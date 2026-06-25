// Copyright (c) Microsoft Corporation.

//! `wordy`'s custom-dictionary store — a per-user, per-locale set of words kept
//! on the filesystem.
//!
//! Where the shared dictionary ([`crate::words`]) is read-only data baked into
//! the binary, the *custom* dictionary is the mutable, per-principal set a user
//! grows with their own words. It is stored as a directory tree
//! `{root}/{locale}/{user}/`, with **each word represented by an empty file
//! whose name encodes the word**. Every operation is therefore a pure
//! namespace / metadata act — create, stat, delete, enumerate — and never reads
//! or writes file *content* (windows-win32-shim SHIM-D6 alignment). That split
//! is deliberate: it keeps `wordy`'s mutable state inside exactly the filesystem
//! surface the isolation harness already redirects.
//!
//! ## Owned behavioral specification (Design Autonomy)
//!
//! - A word is encoded to a filename by [`encode_token`]: lowercased, then every
//!   byte outside `a`–`z` percent-encoded (`%XX`, upper-case hex). The result
//!   contains only `[a-z]` and `%`-escapes, so it can never contain a path
//!   separator, `.`, or `..` — hostile input cannot escape the user directory.
//!   Encoding is reversible via [`decode_token`] (case is folded, so the custom
//!   dictionary — like the shared one — is case-insensitive).
//! - The same encoding sanitizes the **user** component, so an `X-Wordy-User`
//!   header value is always a safe single directory name.
//! - `add` creates the word's marker file (no content), reporting whether it was
//!   newly added; `contains` stats it; `remove` deletes it, reporting whether it
//!   existed; `list` enumerates and decodes the directory, returning the words
//!   in ascending order. A missing user directory reads as an empty dictionary.
//! - The empty word (after trimming) is rejected as invalid input.
//!
//! This module is pure `std::fs` and platform-independent: no IIS and no
//! isolation awareness (SHIM-D19), so it is unit-testable on any platform over a
//! scratch temp directory.

use std::fs;
use std::io;
use std::path::PathBuf;

use crate::words::Locale;

/// The request header `wordy` reads to identify the calling user. Absent or
/// blank, the [`DEFAULT_USER`] is used.
pub const USER_HEADER: &str = "X-Wordy-User";

/// The built-in user used when no [`USER_HEADER`] is supplied.
pub const DEFAULT_USER: &str = "default";

/// A user identity. Today this is just a name resolved from a request header,
/// but it is modeled as a newtype so per-user claims can grow here later
/// (windows-win32-shim SHIM-D19, "forward-compatible identity / locale").
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct UserId(String);

impl UserId {
    /// Build a user id from a raw header value. Leading / trailing whitespace is
    /// trimmed; an empty result falls back to [`DEFAULT_USER`].
    pub fn new(raw: &str) -> Self {
        let trimmed = raw.trim();
        if trimmed.is_empty() {
            UserId(DEFAULT_USER.to_string())
        } else {
            UserId(trimmed.to_string())
        }
    }

    /// The raw user-id string (before path encoding).
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl Default for UserId {
    fn default() -> Self {
        UserId(DEFAULT_USER.to_string())
    }
}

/// The authenticated (today: asserted) caller. `wordy` performs no real logon;
/// it threads a [`Principal`] through every handler as if it did, resolving it
/// from the [`USER_HEADER`] and defaulting to a single built-in user — the
/// "app reads its own claims" posture of a real HWC application.
#[derive(Clone, Debug, PartialEq, Eq, Default)]
pub struct Principal {
    user: UserId,
}

impl Principal {
    /// Build a principal for an explicit user.
    pub fn new(user: UserId) -> Self {
        Principal { user }
    }

    /// Resolve a principal from an optional [`USER_HEADER`] value, defaulting to
    /// the built-in user when absent or blank.
    pub fn from_header(header: Option<&str>) -> Self {
        Principal {
            user: UserId::new(header.unwrap_or("")),
        }
    }

    /// The user this principal represents.
    pub fn user_id(&self) -> &UserId {
        &self.user
    }
}

/// A filesystem-backed custom-dictionary store rooted at a base directory.
///
/// All words for a `(locale, user)` pair live under
/// `{root}/{locale-slug}/{encoded-user}/`, one empty file per word.
#[derive(Clone, Debug)]
pub struct CustomDictionary {
    root: PathBuf,
}

impl CustomDictionary {
    /// Create a store rooted at `root`. The directory tree is created lazily on
    /// the first `add`.
    pub fn new(root: impl Into<PathBuf>) -> Self {
        CustomDictionary { root: root.into() }
    }

    /// The directory holding `user`'s words for `locale`.
    fn user_dir(&self, locale: Locale, user: &Principal) -> PathBuf {
        self.root
            .join(locale.slug())
            .join(encode_token(user.user_id().as_str()))
    }

    /// The marker-file path for `word` in `user`'s `locale` dictionary.
    fn word_path(&self, locale: Locale, user: &Principal, word: &str) -> io::Result<PathBuf> {
        let trimmed = validate_word(word)?;
        Ok(self.user_dir(locale, user).join(encode_token(trimmed)))
    }

    /// Add `word` to `user`'s custom dictionary for `locale`. Returns `Ok(true)`
    /// if the word was newly added, `Ok(false)` if it was already present.
    pub fn add(&self, locale: Locale, user: &Principal, word: &str) -> io::Result<bool> {
        let path = self.word_path(locale, user, word)?;
        // `word_path` validated the word, so the parent always exists here.
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)?;
        }
        // Create an empty marker file; `create_new` distinguishes "added" from
        // "already there". No content is ever written (SHIM-D6 alignment).
        match fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&path)
        {
            Ok(_file) => Ok(true),
            Err(e) if e.kind() == io::ErrorKind::AlreadyExists => Ok(false),
            Err(e) => Err(e),
        }
    }

    /// Whether `word` is in `user`'s custom dictionary for `locale`.
    pub fn contains(&self, locale: Locale, user: &Principal, word: &str) -> io::Result<bool> {
        let path = self.word_path(locale, user, word)?;
        path.try_exists()
    }

    /// Remove `word` from `user`'s custom dictionary for `locale`. Returns
    /// `Ok(true)` if the word existed and was removed, `Ok(false)` otherwise.
    pub fn remove(&self, locale: Locale, user: &Principal, word: &str) -> io::Result<bool> {
        let path = self.word_path(locale, user, word)?;
        match fs::remove_file(&path) {
            Ok(()) => Ok(true),
            Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(false),
            Err(e) => Err(e),
        }
    }

    /// Enumerate all words in `user`'s custom dictionary for `locale`, in
    /// ascending order. A missing directory yields an empty list.
    pub fn list(&self, locale: Locale, user: &Principal) -> io::Result<Vec<String>> {
        let dir = self.user_dir(locale, user);
        let entries = match fs::read_dir(&dir) {
            Ok(entries) => entries,
            Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(Vec::new()),
            Err(e) => return Err(e),
        };

        let mut words = Vec::new();
        for entry in entries {
            let entry = entry?;
            if let Some(name) = entry.file_name().to_str()
                && let Some(word) = decode_token(name)
            {
                words.push(word);
            }
        }
        words.sort();
        Ok(words)
    }
}

/// Trim `word` and reject an empty result.
fn validate_word(word: &str) -> io::Result<&str> {
    let trimmed = word.trim();
    if trimmed.is_empty() {
        Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "custom-dictionary word must not be empty",
        ))
    } else {
        Ok(trimmed)
    }
}

/// Encode a token (word or user id) to a path-safe filename: lowercase, then
/// percent-encode every byte that is not an ASCII `a`–`z` letter.
///
/// The output contains only `[a-z]` and `%XX` escapes, so it can never be a path
/// separator, `.`, or `..`.
pub fn encode_token(token: &str) -> String {
    let lower = token.to_ascii_lowercase();
    let mut out = String::with_capacity(lower.len());
    for &byte in lower.as_bytes() {
        if byte.is_ascii_lowercase() {
            out.push(byte as char);
        } else {
            out.push('%');
            out.push(hex_digit(byte >> 4));
            out.push(hex_digit(byte & 0x0f));
        }
    }
    out
}

/// Decode a filename produced by [`encode_token`] back to its token, or `None`
/// if the name is not a valid encoding.
pub fn decode_token(name: &str) -> Option<String> {
    let bytes = name.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        match bytes[i] {
            b'%' => {
                let hi = hex_value(*bytes.get(i + 1)?)?;
                let lo = hex_value(*bytes.get(i + 2)?)?;
                out.push((hi << 4) | lo);
                i += 3;
            }
            byte @ b'a'..=b'z' => {
                out.push(byte);
                i += 1;
            }
            _ => return None,
        }
    }
    String::from_utf8(out).ok()
}

/// Map a `0..16` nibble to its upper-case hex digit.
fn hex_digit(nibble: u8) -> char {
    match nibble {
        0..=9 => (b'0' + nibble) as char,
        _ => (b'A' + (nibble - 10)) as char,
    }
}

/// Parse a single hex digit byte to its `0..16` value.
fn hex_value(byte: u8) -> Option<u8> {
    match byte {
        b'0'..=b'9' => Some(byte - b'0'),
        b'a'..=b'f' => Some(byte - b'a' + 10),
        b'A'..=b'F' => Some(byte - b'A' + 10),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU32, Ordering};

    /// A unique scratch directory under the OS temp dir, removed on drop so the
    /// suite leaves no artifacts behind.
    struct TempDir {
        path: PathBuf,
    }

    impl TempDir {
        fn new() -> Self {
            static COUNTER: AtomicU32 = AtomicU32::new(0);
            let unique = COUNTER.fetch_add(1, Ordering::Relaxed);
            let mut path = std::env::temp_dir();
            path.push(format!("wordy-custom-{}-{}", std::process::id(), unique));
            std::fs::create_dir_all(&path).expect("create scratch dir");
            TempDir { path }
        }
    }

    impl Drop for TempDir {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.path);
        }
    }

    fn store() -> (TempDir, CustomDictionary) {
        let tmp = TempDir::new();
        let store = CustomDictionary::new(&tmp.path);
        (tmp, store)
    }

    const EN: Locale = Locale::EnUs;

    // --- token encoding ---------------------------------------------------

    #[test]
    fn encode_is_lowercase_and_path_safe() {
        let encoded = encode_token("Don't");
        // No path-dangerous characters survive.
        assert!(!encoded.contains('/'));
        assert!(!encoded.contains('\\'));
        assert!(!encoded.contains('.'));
        assert_eq!(encoded, "don%27t");
    }

    #[test]
    fn encode_decode_round_trips_case_folded() {
        for word in ["hello", "Don't", "co-op", "naïve", "a.b/c", "%weird%"] {
            let decoded = decode_token(&encode_token(word)).unwrap();
            assert_eq!(decoded, word.to_lowercase());
        }
    }

    #[test]
    fn path_escape_attempts_are_neutralized() {
        for hostile in ["../../etc/passwd", "..", ".", "a/b\\c"] {
            let encoded = encode_token(hostile);
            assert!(!encoded.contains('/'));
            assert!(!encoded.contains('\\'));
            assert!(encoded != "." && encoded != "..");
            // Still reversible.
            assert_eq!(decode_token(&encoded).unwrap(), hostile.to_lowercase());
        }
    }

    #[test]
    fn decode_rejects_malformed_names() {
        assert_eq!(decode_token("%"), None);
        assert_eq!(decode_token("%2"), None);
        assert_eq!(decode_token("%2G"), None);
        assert_eq!(decode_token("a/b"), None);
        assert_eq!(decode_token("ABC"), None); // upper-case is never produced
    }

    // --- principal resolution ---------------------------------------------

    #[test]
    fn principal_defaults_when_header_absent_or_blank() {
        assert_eq!(Principal::from_header(None).user_id().as_str(), DEFAULT_USER);
        assert_eq!(
            Principal::from_header(Some("   ")).user_id().as_str(),
            DEFAULT_USER
        );
        assert_eq!(
            Principal::from_header(Some(" alice ")).user_id().as_str(),
            "alice"
        );
    }

    // --- store operations -------------------------------------------------

    #[test]
    fn add_then_contains_and_absent_word_is_false() {
        let (_tmp, store) = store();
        let user = Principal::default();
        assert!(store.add(EN, &user, "kumquat").unwrap());
        assert!(store.contains(EN, &user, "kumquat").unwrap());
        assert!(!store.contains(EN, &user, "rutabaga").unwrap());
    }

    #[test]
    fn add_is_idempotent() {
        let (_tmp, store) = store();
        let user = Principal::default();
        assert!(store.add(EN, &user, "widget").unwrap());
        assert!(!store.add(EN, &user, "widget").unwrap());
    }

    #[test]
    fn remove_reports_prior_existence() {
        let (_tmp, store) = store();
        let user = Principal::default();
        store.add(EN, &user, "gadget").unwrap();
        assert!(store.remove(EN, &user, "gadget").unwrap());
        assert!(!store.remove(EN, &user, "gadget").unwrap());
        assert!(!store.contains(EN, &user, "gadget").unwrap());
    }

    #[test]
    fn list_returns_sorted_words_and_empty_when_absent() {
        let (_tmp, store) = store();
        let user = Principal::default();
        assert!(store.list(EN, &user).unwrap().is_empty());
        store.add(EN, &user, "zebra").unwrap();
        store.add(EN, &user, "apple").unwrap();
        store.add(EN, &user, "mango").unwrap();
        assert_eq!(store.list(EN, &user).unwrap(), vec!["apple", "mango", "zebra"]);
    }

    #[test]
    fn membership_and_listing_are_case_folded() {
        let (_tmp, store) = store();
        let user = Principal::default();
        assert!(store.add(EN, &user, "Hello").unwrap());
        assert!(store.contains(EN, &user, "hello").unwrap());
        assert!(store.contains(EN, &user, "HELLO").unwrap());
        // Re-adding the differently-cased form is a no-op.
        assert!(!store.add(EN, &user, "HELLO").unwrap());
        assert_eq!(store.list(EN, &user).unwrap(), vec!["hello"]);
    }

    #[test]
    fn words_with_punctuation_round_trip_through_storage() {
        let (_tmp, store) = store();
        let user = Principal::default();
        store.add(EN, &user, "don't").unwrap();
        store.add(EN, &user, "co-op").unwrap();
        assert!(store.contains(EN, &user, "don't").unwrap());
        assert_eq!(store.list(EN, &user).unwrap(), vec!["co-op", "don't"]);
    }

    #[test]
    fn users_are_isolated() {
        let (_tmp, store) = store();
        let alice = Principal::from_header(Some("alice"));
        let bob = Principal::from_header(Some("bob"));
        store.add(EN, &alice, "secret").unwrap();
        assert!(store.contains(EN, &alice, "secret").unwrap());
        assert!(!store.contains(EN, &bob, "secret").unwrap());
        assert!(store.list(EN, &bob).unwrap().is_empty());
    }

    #[test]
    fn hostile_word_stays_within_user_directory() {
        let (tmp, store) = store();
        let user = Principal::default();
        store.add(EN, &user, "../../escape").unwrap();
        // Exactly one marker file exists, and it sits inside the user dir.
        let user_dir = tmp.path.join(EN.slug()).join(DEFAULT_USER);
        let entries: Vec<_> = std::fs::read_dir(&user_dir)
            .unwrap()
            .map(|e| e.unwrap().file_name())
            .collect();
        assert_eq!(entries.len(), 1);
        // Nothing leaked above the root.
        assert!(store.contains(EN, &user, "../../escape").unwrap());
        assert_eq!(store.list(EN, &user).unwrap(), vec!["../../escape"]);
    }

    #[test]
    fn empty_word_is_invalid_input() {
        let (_tmp, store) = store();
        let user = Principal::default();
        for op in ["", "   ", "\t"] {
            let err = store.add(EN, &user, op).unwrap_err();
            assert_eq!(err.kind(), io::ErrorKind::InvalidInput);
        }
    }

    #[test]
    fn storage_layout_is_locale_then_user() {
        let (tmp, store) = store();
        let user = Principal::from_header(Some("alice"));
        store.add(EN, &user, "word").unwrap();
        let expected_dir = tmp.path.join("en-US").join("alice");
        assert!(expected_dir.is_dir());
    }
}
