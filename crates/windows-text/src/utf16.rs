// Copyright (c) Microsoft Corporation.

//! The [`Utf16`] owned string type (D7/D9).

use core::fmt;

use crate::error::{Error, Result};

/// An owned UTF-16 string: the internal Windows string representation (D7).
///
/// Exact-equality (`PartialEq`/`Eq`/`Hash`) is over raw code units and is
/// **case-sensitive**. Ordinal case-insensitive comparison and ordering go
/// through [`Utf16::compare_ignore_case`] / [`Utf16::sort_key`] (Windows) or the
/// [`OrdinalCasing`](crate::OrdinalCasing) trait, never through these impls.
///
/// The stored code units may be ill-formed UTF-16 (for example, unpaired
/// surrogates from the OS): they are kept losslessly, and well-formedness is
/// enforced only at UTF-8 egress, which is fallible (D9).
#[derive(Clone, PartialEq, Eq, Hash)]
pub struct Utf16(Vec<u16>);

impl Utf16 {
    /// Ingest UTF-8 (`&str`) into UTF-16 storage. Always succeeds: well-formed
    /// UTF-8 maps to well-formed UTF-16.
    #[must_use]
    pub fn from_utf8(s: &str) -> Self {
        Self(s.encode_utf16().collect())
    }

    /// Wrap raw UTF-16 code units losslessly (D9). The units may be ill-formed
    /// (for example, unpaired surrogates); they are preserved as given.
    #[must_use]
    pub fn from_units(units: Vec<u16>) -> Self {
        Self(units)
    }

    /// The raw UTF-16 code units.
    #[must_use]
    pub fn as_units(&self) -> &[u16] {
        &self.0
    }

    /// Egress to UTF-8 (D9). Fails with [`Error::IllFormedUtf16`] if the stored
    /// units are not well-formed UTF-16 — never panics, never substitutes
    /// replacement characters.
    ///
    /// # Errors
    ///
    /// Returns [`Error::IllFormedUtf16`] when the code units contain an unpaired
    /// surrogate or other ill-formed sequence.
    pub fn to_utf8(&self) -> Result<String> {
        char::decode_utf16(self.0.iter().copied())
            .collect::<core::result::Result<String, _>>()
            .map_err(|_| Error::IllFormedUtf16)
    }

    /// Number of UTF-16 code units.
    #[must_use]
    pub fn len(&self) -> usize {
        self.0.len()
    }

    /// Whether the string has no code units.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.0.is_empty()
    }
}

impl From<&str> for Utf16 {
    fn from(s: &str) -> Self {
        Self::from_utf8(s)
    }
}

impl From<String> for Utf16 {
    fn from(s: String) -> Self {
        Self::from_utf8(&s)
    }
}

impl fmt::Debug for Utf16 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // Lossy purely for diagnostics; never used as real egress (D9).
        write!(f, "Utf16({:?})", String::from_utf16_lossy(&self.0))
    }
}
