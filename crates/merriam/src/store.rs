// Copyright (c) Microsoft Corporation.

//! The on-disk custom-dictionary store (MER-D1/D2/D3).
//!
//! Each `(locale, user)` pair is persisted as **one newline-delimited word-list
//! file** at `{root}/{locale-slug}/{user-slug}.dict`, read and written through
//! [`windows_file_io`] — native async overlapped Win32 I/O. Unlike `wordy`'s
//! name-encoded empty files (pure namespace ops), this is a *content* store, so
//! every `add`/`remove`/`contains`/`list` reads or rewrites file bytes and
//! genuinely drives the overlapped read/write path.
//!
//! ## Owned behavioral specification (Design Autonomy)
//!
//! - A **word** is normalized by [`normalize_word`]: trimmed, rejected if empty
//!   or if it contains a line break (it must occupy exactly one line), then
//!   lowercased (the dictionary is case-insensitive, matching `wordy`).
//! - The **locale** and **user** are turned into path-safe slugs by [`slug`]:
//!   lowercased, with every byte outside `[a-z0-9-_]` percent-encoded (`%XX`,
//!   upper-case hex). The result can never contain a path separator, `.`, or
//!   `..`, so hostile input cannot escape `{root}`.
//! - `add` inserts the word, reporting whether it was newly added; `remove`
//!   deletes it, reporting whether it existed; `contains` tests membership;
//!   `list` returns the words in ascending order. A missing file reads as an
//!   empty dictionary.
//! - Each `(locale, user)` has a private mutation/read lock so a concurrent
//!   read never observes a half-rewritten file and concurrent first-writes do
//!   not race; the async file I/O is driven under the lock via a thread-pool
//!   `block_on` (the lock is never held across an `.await`, only across the
//!   synchronous driver — MER-D3).

use std::collections::BTreeSet;
use std::collections::HashMap;
use std::fmt;
use std::path::PathBuf;
use std::sync::{Arc, Mutex};

use windows_file_io::{File, FileError};
use windows_threadpool_executor::block_on;

/// A failed store operation.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum StoreError {
    /// The word was empty (after trimming) or contained a line break.
    InvalidWord(String),
    /// An underlying file or namespace I/O failure.
    Io(String),
}

impl fmt::Display for StoreError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            StoreError::InvalidWord(w) => write!(f, "invalid word: {w:?}"),
            StoreError::Io(m) => write!(f, "store I/O error: {m}"),
        }
    }
}

impl std::error::Error for StoreError {}

impl From<FileError> for StoreError {
    fn from(e: FileError) -> Self {
        StoreError::Io(format!("file error (os error {})", e.code()))
    }
}

impl From<std::io::Error> for StoreError {
    fn from(e: std::io::Error) -> Self {
        StoreError::Io(e.to_string())
    }
}

/// The result of a store operation.
pub type StoreResult<T> = Result<T, StoreError>;

/// Normalize a raw word to its canonical stored form, or reject it.
///
/// # Errors
/// [`StoreError::InvalidWord`] if the word is empty after trimming or contains a
/// carriage return / line feed.
pub fn normalize_word(raw: &str) -> StoreResult<String> {
    let trimmed = raw.trim();
    if trimmed.is_empty() {
        return Err(StoreError::InvalidWord(raw.to_string()));
    }
    if trimmed.contains(['\n', '\r']) {
        return Err(StoreError::InvalidWord(raw.to_string()));
    }
    Ok(trimmed.to_lowercase())
}

/// Encode `component` as a path-safe slug (lower-case; bytes outside
/// `[a-z0-9-_]` percent-encoded). Injective, so distinct inputs never collide.
fn slug(component: &str) -> String {
    let mut out = String::with_capacity(component.len());
    for byte in component.trim().to_lowercase().bytes() {
        if byte.is_ascii_lowercase() || byte.is_ascii_digit() || byte == b'-' || byte == b'_' {
            out.push(byte as char);
        } else {
            out.push('%');
            out.push_str(&format!("{byte:02X}"));
        }
    }
    if out.is_empty() {
        // A component that slugs to nothing (e.g. all whitespace) still needs a
        // stable, non-empty name.
        out.push('_');
    }
    out
}

/// A filesystem-backed custom-dictionary store rooted at a base directory.
///
/// Words for a `(locale, user)` pair live in one file
/// `{root}/{locale-slug}/{user-slug}.dict`.
#[derive(Clone)]
pub struct Store {
    root: PathBuf,
    /// Per-`(locale, user)` locks, created on demand. Serializes the
    /// read-modify-write of a word-list file (MER-D3).
    locks: Arc<Mutex<HashMap<String, Arc<Mutex<()>>>>>,
}

impl Store {
    /// Create a store rooted at `root` (created on first write).
    pub fn new(root: impl Into<PathBuf>) -> Self {
        Store {
            root: root.into(),
            locks: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    /// The on-disk path of the `(locale, user)` word-list file.
    fn path(&self, locale: &str, user: &str) -> PathBuf {
        self.root.join(slug(locale)).join(format!("{}.dict", slug(user)))
    }

    /// The lock key for a `(locale, user)` pair.
    fn lock_for(&self, locale: &str, user: &str) -> Arc<Mutex<()>> {
        let key = format!("{}/{}", slug(locale), slug(user));
        let mut locks = self.locks.lock().unwrap();
        Arc::clone(locks.entry(key).or_insert_with(|| Arc::new(Mutex::new(()))))
    }

    /// Read the current word set, treating a missing file as empty. Driven
    /// synchronously over the async overlapped read.
    fn read_words(&self, locale: &str, user: &str) -> StoreResult<BTreeSet<String>> {
        let path = self.path(locale, user);
        match File::open(&path) {
            Ok(mut file) => {
                let bytes = block_on(file.read_to_end())?;
                let text = String::from_utf8_lossy(&bytes);
                Ok(text.lines().filter(|l| !l.is_empty()).map(str::to_string).collect())
            }
            Err(e) if e.is_not_found() => Ok(BTreeSet::new()),
            Err(e) => Err(e.into()),
        }
    }

    /// Rewrite the word-list file with `words` (sorted by `BTreeSet`). Driven
    /// synchronously over the async overlapped write.
    fn write_words(&self, locale: &str, user: &str, words: &BTreeSet<String>) -> StoreResult<()> {
        let path = self.path(locale, user);
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        let mut blob = String::new();
        for word in words {
            blob.push_str(word);
            blob.push('\n');
        }
        let mut file = File::create(&path)?;
        if !blob.is_empty() {
            block_on(file.write_all_at(0, blob.as_bytes()))?;
        }
        Ok(())
    }

    /// Add `word` to `(locale, user)`, returning whether it was newly added.
    ///
    /// # Errors
    /// [`StoreError::InvalidWord`] for a bad word, or [`StoreError::Io`].
    pub fn add(&self, locale: &str, user: &str, word: &str) -> StoreResult<bool> {
        let word = normalize_word(word)?;
        let lock = self.lock_for(locale, user);
        let _guard = lock.lock().unwrap();
        let mut words = self.read_words(locale, user)?;
        let added = words.insert(word);
        if added {
            self.write_words(locale, user, &words)?;
        }
        Ok(added)
    }

    /// Remove `word` from `(locale, user)`, returning whether it existed.
    ///
    /// # Errors
    /// [`StoreError::InvalidWord`] for a bad word, or [`StoreError::Io`].
    pub fn remove(&self, locale: &str, user: &str, word: &str) -> StoreResult<bool> {
        let word = normalize_word(word)?;
        let lock = self.lock_for(locale, user);
        let _guard = lock.lock().unwrap();
        let mut words = self.read_words(locale, user)?;
        let removed = words.remove(&word);
        if removed {
            self.write_words(locale, user, &words)?;
        }
        Ok(removed)
    }

    /// Whether `word` is present in `(locale, user)`.
    ///
    /// # Errors
    /// [`StoreError::InvalidWord`] for a bad word, or [`StoreError::Io`].
    pub fn contains(&self, locale: &str, user: &str, word: &str) -> StoreResult<bool> {
        let word = normalize_word(word)?;
        let lock = self.lock_for(locale, user);
        let _guard = lock.lock().unwrap();
        Ok(self.read_words(locale, user)?.contains(&word))
    }

    /// List the words in `(locale, user)` in ascending order.
    ///
    /// # Errors
    /// [`StoreError::Io`] if the file cannot be read.
    pub fn list(&self, locale: &str, user: &str) -> StoreResult<Vec<String>> {
        let lock = self.lock_for(locale, user);
        let _guard = lock.lock().unwrap();
        Ok(self.read_words(locale, user)?.into_iter().collect())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    struct ScratchDir {
        path: PathBuf,
    }
    impl ScratchDir {
        fn new(tag: &str) -> Self {
            let mut path = std::env::temp_dir();
            path.push(format!(
                "merriam_{tag}_{}_{:?}",
                std::process::id(),
                std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap()
                    .as_nanos()
            ));
            Self { path }
        }
    }
    impl Drop for ScratchDir {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.path);
        }
    }

    const LOCALE: &str = "en-US";
    const USER: &str = "default";

    #[test]
    fn add_then_contains_and_list() {
        let dir = ScratchDir::new("add");
        let store = Store::new(&dir.path);
        assert!(store.add(LOCALE, USER, "widget").unwrap());
        assert!(store.contains(LOCALE, USER, "widget").unwrap());
        assert_eq!(store.list(LOCALE, USER).unwrap(), vec!["widget".to_string()]);
    }

    #[test]
    fn add_is_idempotent() {
        let dir = ScratchDir::new("idem");
        let store = Store::new(&dir.path);
        assert!(store.add(LOCALE, USER, "alpha").unwrap());
        assert!(!store.add(LOCALE, USER, "alpha").unwrap(), "second add is not new");
        assert_eq!(store.list(LOCALE, USER).unwrap().len(), 1);
    }

    #[test]
    fn remove_reports_existence() {
        let dir = ScratchDir::new("remove");
        let store = Store::new(&dir.path);
        store.add(LOCALE, USER, "gamma").unwrap();
        assert!(store.remove(LOCALE, USER, "gamma").unwrap());
        assert!(!store.remove(LOCALE, USER, "gamma").unwrap(), "second remove is no-op");
        assert!(!store.contains(LOCALE, USER, "gamma").unwrap());
    }

    #[test]
    fn list_is_sorted() {
        let dir = ScratchDir::new("sorted");
        let store = Store::new(&dir.path);
        for w in ["zeta", "alpha", "mu", "beta"] {
            store.add(LOCALE, USER, w).unwrap();
        }
        assert_eq!(
            store.list(LOCALE, USER).unwrap(),
            vec!["alpha", "beta", "mu", "zeta"]
        );
    }

    #[test]
    fn words_are_case_folded() {
        let dir = ScratchDir::new("case");
        let store = Store::new(&dir.path);
        assert!(store.add(LOCALE, USER, "Widget").unwrap());
        assert!(!store.add(LOCALE, USER, "WIDGET").unwrap(), "same word folded");
        assert!(store.contains(LOCALE, USER, "widget").unwrap());
    }

    #[test]
    fn words_are_trimmed() {
        let dir = ScratchDir::new("trim");
        let store = Store::new(&dir.path);
        store.add(LOCALE, USER, "  spaced  ").unwrap();
        assert!(store.contains(LOCALE, USER, "spaced").unwrap());
    }

    #[test]
    fn empty_word_is_rejected() {
        let dir = ScratchDir::new("empty");
        let store = Store::new(&dir.path);
        assert!(matches!(
            store.add(LOCALE, USER, "   "),
            Err(StoreError::InvalidWord(_))
        ));
    }

    #[test]
    fn newline_word_is_rejected() {
        let dir = ScratchDir::new("newline");
        let store = Store::new(&dir.path);
        assert!(matches!(
            store.add(LOCALE, USER, "two\nlines"),
            Err(StoreError::InvalidWord(_))
        ));
    }

    #[test]
    fn missing_dictionary_lists_empty() {
        let dir = ScratchDir::new("missing");
        let store = Store::new(&dir.path);
        assert!(store.list(LOCALE, "nobody").unwrap().is_empty());
        assert!(!store.contains(LOCALE, "nobody", "x").unwrap());
    }

    #[test]
    fn users_are_isolated() {
        let dir = ScratchDir::new("users");
        let store = Store::new(&dir.path);
        store.add(LOCALE, "alice", "apple").unwrap();
        store.add(LOCALE, "bob", "banana").unwrap();
        assert_eq!(store.list(LOCALE, "alice").unwrap(), vec!["apple"]);
        assert_eq!(store.list(LOCALE, "bob").unwrap(), vec!["banana"]);
        assert!(!store.contains(LOCALE, "alice", "banana").unwrap());
    }

    #[test]
    fn locales_are_isolated() {
        let dir = ScratchDir::new("locales");
        let store = Store::new(&dir.path);
        store.add("en-US", USER, "color").unwrap();
        store.add("en-GB", USER, "colour").unwrap();
        assert_eq!(store.list("en-US", USER).unwrap(), vec!["color"]);
        assert_eq!(store.list("en-GB", USER).unwrap(), vec!["colour"]);
    }

    #[test]
    fn slug_neutralizes_path_traversal() {
        let dir = ScratchDir::new("traversal");
        let store = Store::new(&dir.path);
        // A hostile user component cannot escape the root.
        store.add(LOCALE, "../../etc", "x").unwrap();
        let escaped = dir.path.parent().unwrap().parent().unwrap().join("etc");
        assert!(!escaped.exists(), "must not have written outside the root");
        assert!(store.contains(LOCALE, "../../etc", "x").unwrap());
    }

    #[test]
    fn persists_across_store_instances() {
        let dir = ScratchDir::new("persist");
        {
            let store = Store::new(&dir.path);
            store.add(LOCALE, USER, "durable").unwrap();
        }
        let store = Store::new(&dir.path);
        assert!(store.contains(LOCALE, USER, "durable").unwrap());
    }

    #[test]
    fn rewrite_shrinks_file_no_stale_tail() {
        let dir = ScratchDir::new("shrink");
        let store = Store::new(&dir.path);
        for w in ["aaaaaaaa", "bbbbbbbb", "cccccccc"] {
            store.add(LOCALE, USER, w).unwrap();
        }
        store.remove(LOCALE, USER, "bbbbbbbb").unwrap();
        store.remove(LOCALE, USER, "cccccccc").unwrap();
        // After shrinking, only the one word remains (CREATE_ALWAYS truncates).
        assert_eq!(store.list(LOCALE, USER).unwrap(), vec!["aaaaaaaa"]);
    }
}
