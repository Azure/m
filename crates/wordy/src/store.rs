// Copyright (c) Microsoft Corporation.

//! The custom-dictionary store abstraction (WD-D13).
//!
//! `wordy`'s route handlers depend on the [`CustomStore`] trait rather than a
//! concrete store, so the per-user custom dictionary can live anywhere. In
//! production it lives in `merriam`, reached over WinHTTP by the relay
//! (`RelayStore`, Windows only); in tests it is an in-memory [`MemoryStore`].
//! The trait and `MemoryStore` are platform-independent, so the whole routing
//! surface stays unit-testable on any platform without a network.

use std::collections::{BTreeSet, HashMap};
use std::fmt;
use std::sync::Mutex;

use crate::identity::Principal;
use crate::words::Locale;

/// A failed custom-store operation.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum StoreError {
    /// The word was empty (after trimming) or otherwise invalid — a client error.
    InvalidWord(String),
    /// The backing service answered with a non-200 status.
    Upstream {
        /// The HTTP status code.
        status: u16,
        /// The response body.
        body: String,
    },
    /// The store could not be reached / a transport or internal failure.
    Transport(String),
}

impl fmt::Display for StoreError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            StoreError::InvalidWord(w) => write!(f, "invalid word: {w}"),
            StoreError::Upstream { status, body } => write!(f, "store returned {status}: {body}"),
            StoreError::Transport(m) => write!(f, "store transport error: {m}"),
        }
    }
}

impl std::error::Error for StoreError {}

/// A per-`(locale, user)` custom dictionary of words.
///
/// The owned behavioral contract (Design Autonomy): a word is trimmed, rejected
/// if empty ([`StoreError::InvalidWord`]), and case-folded (the dictionary is
/// case-insensitive). `add` reports whether the word was newly added; `remove`
/// whether it existed; `list` returns words in ascending order; dictionaries are
/// isolated per `(locale, user)`.
pub trait CustomStore {
    /// Add `word`, returning whether it was newly added.
    ///
    /// # Errors
    /// [`StoreError`] for an invalid word or a store failure.
    fn add(&self, locale: Locale, user: &Principal, word: &str) -> Result<bool, StoreError>;

    /// Remove `word`, returning whether it existed.
    ///
    /// # Errors
    /// [`StoreError`] for an invalid word or a store failure.
    fn remove(&self, locale: Locale, user: &Principal, word: &str) -> Result<bool, StoreError>;

    /// Whether `word` is present.
    ///
    /// # Errors
    /// [`StoreError`] for an invalid word or a store failure.
    fn contains(&self, locale: Locale, user: &Principal, word: &str) -> Result<bool, StoreError>;

    /// List the words in ascending order.
    ///
    /// # Errors
    /// [`StoreError`] for a store failure.
    fn list(&self, locale: Locale, user: &Principal) -> Result<Vec<String>, StoreError>;
}

/// Normalize a word to its canonical stored form, or reject it.
fn normalize(word: &str) -> Result<String, StoreError> {
    let trimmed = word.trim();
    if trimmed.is_empty() {
        return Err(StoreError::InvalidWord(word.to_string()));
    }
    Ok(trimmed.to_lowercase())
}

/// An in-memory [`CustomStore`], used to drive the routing surface in tests with
/// no network and no filesystem. Faithfully replicates the observable contract
/// (trim, case-fold, reject-empty, sorted, per-`(locale, user)` isolation).
#[derive(Default)]
pub struct MemoryStore {
    dictionaries: Mutex<HashMap<(String, String), BTreeSet<String>>>,
}

impl MemoryStore {
    /// Create an empty store.
    pub fn new() -> Self {
        Self::default()
    }

    fn key(locale: Locale, user: &Principal) -> (String, String) {
        (locale.slug().to_string(), user.user_id().as_str().to_string())
    }
}

impl CustomStore for MemoryStore {
    fn add(&self, locale: Locale, user: &Principal, word: &str) -> Result<bool, StoreError> {
        let word = normalize(word)?;
        let mut map = self.dictionaries.lock().unwrap();
        Ok(map.entry(Self::key(locale, user)).or_default().insert(word))
    }

    fn remove(&self, locale: Locale, user: &Principal, word: &str) -> Result<bool, StoreError> {
        let word = normalize(word)?;
        let mut map = self.dictionaries.lock().unwrap();
        Ok(map.entry(Self::key(locale, user)).or_default().remove(&word))
    }

    fn contains(&self, locale: Locale, user: &Principal, word: &str) -> Result<bool, StoreError> {
        let word = normalize(word)?;
        let map = self.dictionaries.lock().unwrap();
        Ok(map.get(&Self::key(locale, user)).is_some_and(|set| set.contains(&word)))
    }

    fn list(&self, locale: Locale, user: &Principal) -> Result<Vec<String>, StoreError> {
        let map = self.dictionaries.lock().unwrap();
        Ok(map
            .get(&Self::key(locale, user))
            .map(|set| set.iter().cloned().collect())
            .unwrap_or_default())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn user(name: &str) -> Principal {
        Principal::from_header(Some(name))
    }

    #[test]
    fn add_contains_list_round_trip() {
        let store = MemoryStore::new();
        assert!(store.add(Locale::EnUs, &user("u"), "widget").unwrap());
        assert!(store.contains(Locale::EnUs, &user("u"), "widget").unwrap());
        assert_eq!(store.list(Locale::EnUs, &user("u")).unwrap(), vec!["widget"]);
    }

    #[test]
    fn add_is_idempotent_and_case_folded() {
        let store = MemoryStore::new();
        assert!(store.add(Locale::EnUs, &user("u"), "Widget").unwrap());
        assert!(!store.add(Locale::EnUs, &user("u"), "WIDGET").unwrap());
        assert_eq!(store.list(Locale::EnUs, &user("u")).unwrap(), vec!["widget"]);
    }

    #[test]
    fn remove_reports_existence() {
        let store = MemoryStore::new();
        store.add(Locale::EnUs, &user("u"), "gone").unwrap();
        assert!(store.remove(Locale::EnUs, &user("u"), "gone").unwrap());
        assert!(!store.remove(Locale::EnUs, &user("u"), "gone").unwrap());
    }

    #[test]
    fn empty_word_is_invalid() {
        let store = MemoryStore::new();
        assert!(matches!(
            store.add(Locale::EnUs, &user("u"), "  "),
            Err(StoreError::InvalidWord(_))
        ));
    }

    #[test]
    fn users_are_isolated() {
        let store = MemoryStore::new();
        store.add(Locale::EnUs, &user("alice"), "apple").unwrap();
        assert!(store.list(Locale::EnUs, &user("bob")).unwrap().is_empty());
    }

    #[test]
    fn list_is_sorted() {
        let store = MemoryStore::new();
        for w in ["zeta", "alpha", "mu"] {
            store.add(Locale::EnUs, &user("u"), w).unwrap();
        }
        assert_eq!(store.list(Locale::EnUs, &user("u")).unwrap(), vec!["alpha", "mu", "zeta"]);
    }
}
