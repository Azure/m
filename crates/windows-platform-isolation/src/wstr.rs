// Copyright (c) Microsoft Corporation.

//! UTF-16 string storage and the ordinal-casing seam.
//!
//! Per D7, strings are stored internally as UTF-16 code units ([`Utf16`]); the
//! public boundary ingests/produces UTF-8. Per D9, stored OS strings may be
//! ill-formed UTF-16 (e.g. lone surrogates): they are kept losslessly, and
//! well-formedness is enforced only at UTF-8 egress, which is fallible.
//!
//! Per D6/D8, ordinal case-insensitive comparison and binary sort-key generation
//! are abstracted behind the [`OrdinalCasing`] trait. The **only production**
//! implementation is the mandated Win32 one (`CompareStringOrdinal` /
//! `LCMapStringEx`). Per D16, the trait, the `Utf16` type, and that production
//! implementation are being relocated into the standalone `windows-text`
//! crate (CHECKLIST M2/M3); after M3 this module is retired and these types are
//! re-exported from that crate. The safe core stays FFI-free by programming
//! against the trait; the test-only ASCII implementation (`AsciiOrdinalCasing`,
//! compiled under `#[cfg(test)]`) must never ship as the production comparator.

use core::cmp::Ordering;
use core::fmt;

use crate::error::{RegistryError, Result};

/// An owned UTF-16 string (internal representation, D7).
///
/// Exact-equality (`PartialEq`/`Eq`/`Hash`) is over raw code units and is
/// **case-sensitive**. Case-insensitive comparison and ordering go through
/// [`OrdinalCasing`], never through these impls.
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
    /// (e.g. unpaired surrogates); they are preserved as given.
    #[must_use]
    pub fn from_units(units: Vec<u16>) -> Self {
        Self(units)
    }

    /// The raw UTF-16 code units.
    #[must_use]
    pub fn as_units(&self) -> &[u16] {
        &self.0
    }

    /// Egress to UTF-8 (D9). Fails with [`RegistryError::IllFormedUtf16`] if the
    /// stored units are not well-formed UTF-16 — never panics, never substitutes
    /// replacement characters.
    pub fn to_utf8(&self) -> Result<String> {
        char::decode_utf16(self.0.iter().copied())
            .collect::<core::result::Result<String, _>>()
            .map_err(|_| RegistryError::IllFormedUtf16)
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

/// The ordinal-casing seam (D6/D8).
///
/// Provides Windows ordinal case-insensitive comparison and binary sort-key
/// generation over UTF-16 code units. Implementations must agree with the OS
/// (the production impl *is* the OS, via Win32). A sort key must yield the same
/// ordinal case-insensitive equality and ordering under a plain byte compare as
/// [`compare_ignore_case`](OrdinalCasing::compare_ignore_case) does directly.
pub trait OrdinalCasing {
    /// Ordinal case-insensitive comparison of two code-unit sequences (D6).
    fn compare_ignore_case(&self, a: &[u16], b: &[u16]) -> Ordering;

    /// Opaque binary sort key for ordinal case-insensitive ordering/equality
    /// via byte comparison (D8).
    fn sort_key(&self, s: &[u16]) -> Vec<u8>;
}

#[cfg(test)]
pub(crate) use test_casing::AsciiOrdinalCasing;

#[cfg(test)]
mod test_casing {
    use super::{OrdinalCasing, Ordering};

    /// Test-only ordinal casing that folds **ASCII** `a-z` to `A-Z` only.
    ///
    /// This is a stand-in so the safe core can be exercised without FFI; it is
    /// compiled solely under `#[cfg(test)]` and **must never** be used as the
    /// production comparator. The production implementation is the mandated
    /// Win32 one (D6/D8), which also folds non-ASCII per the OS.
    pub(crate) struct AsciiOrdinalCasing;

    fn fold(u: u16) -> u16 {
        if (b'a' as u16..=b'z' as u16).contains(&u) {
            u - 32
        } else {
            u
        }
    }

    impl OrdinalCasing for AsciiOrdinalCasing {
        fn compare_ignore_case(&self, a: &[u16], b: &[u16]) -> Ordering {
            a.iter().map(|&u| fold(u)).cmp(b.iter().map(|&u| fold(u)))
        }

        fn sort_key(&self, s: &[u16]) -> Vec<u8> {
            // Big-endian so a plain byte compare reproduces code-unit order.
            let mut out = Vec::with_capacity(s.len() * 2);
            for &u in s {
                out.extend_from_slice(&fold(u).to_be_bytes());
            }
            out
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn utf8_round_trip() {
        for s in [
            "",
            "Software",
            "HKEY_LOCAL_MACHINE",
            "café",
            "日本語",
            "emoji 🦀 here",
        ] {
            let w = Utf16::from_utf8(s);
            assert_eq!(w.to_utf8().unwrap(), s);
        }
    }

    #[test]
    fn ill_formed_utf16_rejected_at_egress() {
        // Lone high surrogate — not well-formed UTF-16.
        let w = Utf16::from_units(vec![0x0041, 0xD800, 0x0042]);
        assert_eq!(w.to_utf8(), Err(RegistryError::IllFormedUtf16));
        // ...but the raw units are preserved losslessly (D9).
        assert_eq!(w.as_units(), &[0x0041, 0xD800, 0x0042]);
        assert_eq!(w.len(), 3);
    }

    #[test]
    fn exact_equality_is_case_sensitive() {
        assert_ne!(Utf16::from_utf8("Foo"), Utf16::from_utf8("foo"));
        assert_eq!(Utf16::from_utf8("Foo"), Utf16::from_utf8("Foo"));
    }

    #[test]
    fn ascii_casing_equality_is_case_insensitive() {
        let c = AsciiOrdinalCasing;
        let a = Utf16::from_utf8("Software");
        let b = Utf16::from_utf8("SOFTWARE");
        assert_eq!(
            c.compare_ignore_case(a.as_units(), b.as_units()),
            Ordering::Equal
        );
        assert_eq!(c.sort_key(a.as_units()), c.sort_key(b.as_units()));
    }

    #[test]
    fn ascii_casing_orders_ignoring_case() {
        let c = AsciiOrdinalCasing;
        let apple = Utf16::from_utf8("apple");
        let banana = Utf16::from_utf8("Banana");
        assert_eq!(
            c.compare_ignore_case(apple.as_units(), banana.as_units()),
            Ordering::Less
        );
        // Sort keys reproduce the same ordering under byte compare (D8).
        assert!(c.sort_key(apple.as_units()) < c.sort_key(banana.as_units()));
    }

    #[test]
    fn sort_key_prefix_orders_before_longer() {
        let c = AsciiOrdinalCasing;
        let short = Utf16::from_utf8("app");
        let long = Utf16::from_utf8("apple");
        assert!(c.sort_key(short.as_units()) < c.sort_key(long.as_units()));
    }
}
