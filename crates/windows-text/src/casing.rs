// Copyright (c) Microsoft Corporation.

//! The ordinal-casing seam (D6/D8): the [`OrdinalCasing`] trait, the Windows
//! production implementation, and a feature-gated ASCII reference.

use core::cmp::Ordering;

use crate::utf16::Utf16;

/// The ordinal-casing seam (D6/D8).
///
/// Provides Windows case-insensitive comparison and a binary sort key over
/// UTF-16 code units. [`compare_ignore_case`](OrdinalCasing::compare_ignore_case)
/// is **ordinal** (never linguistic). [`sort_key`](OrdinalCasing::sort_key) is an
/// `LCMAP_SORTKEY` byte key over the invariant locale: two keys are byte-equal
/// exactly when the comparator reports case-insensitive equality, but the key's
/// byte *ordering* follows invariant linguistic collation and may diverge from
/// the ordinal comparator's ordering on punctuation. The byte form is
/// concatenable into a single `memcmp`-comparable key for compound (multi-field)
/// ordered maps.
///
/// This trait is the dependency-injection seam that lets downstream crates
/// unit-test case-insensitive logic **off Windows** by substituting the
/// [`AsciiOrdinalCasing`] reference for the Win32 implementation.
pub trait OrdinalCasing {
    /// Ordinal case-insensitive comparison of two code-unit sequences (D6).
    fn compare_ignore_case(&self, a: &[u16], b: &[u16]) -> Ordering;

    /// Binary case-insensitive sort key: byte-equal keys correspond to
    /// case-insensitive equality, and the key induces a stable total order for
    /// ordered storage (D8).
    fn sort_key(&self, s: &[u16]) -> Vec<u8>;
}

/// Serialize folded UTF-16 code units big-endian so that a plain byte
/// comparison reproduces code-unit ordering. Used only by the ASCII reference.
#[cfg(any(test, feature = "testing"))]
fn units_to_be_bytes(units: &[u16]) -> Vec<u8> {
    let mut out = Vec::with_capacity(units.len() * 2);
    for &u in units {
        out.extend_from_slice(&u.to_be_bytes());
    }
    out
}

/// The mandated Windows production casing (D6/D8).
///
/// `compare_ignore_case` calls `CompareStringOrdinal(bIgnoreCase = TRUE)`;
/// `sort_key` returns the raw byte key from
/// `LCMapStringEx(LCMAP_SORTKEY | NORM_IGNORECASE)` over the invariant locale.
/// Both delegate to the [`windows-text-sys`](windows_text_sys) leaf, where all
/// `unsafe` is confined.
#[cfg(windows)]
#[derive(Debug, Clone, Copy, Default)]
pub struct Win32OrdinalCasing;

#[cfg(windows)]
impl OrdinalCasing for Win32OrdinalCasing {
    fn compare_ignore_case(&self, a: &[u16], b: &[u16]) -> Ordering {
        windows_text_sys::compare_ordinal_ignore_case(a, b)
    }

    fn sort_key(&self, s: &[u16]) -> Vec<u8> {
        windows_text_sys::sort_key(s)
    }
}

#[cfg(windows)]
impl Utf16 {
    /// Ordinal case-insensitive comparison against another [`Utf16`] (D6).
    #[must_use]
    pub fn compare_ignore_case(&self, other: &Utf16) -> Ordering {
        windows_text_sys::compare_ordinal_ignore_case(self.as_units(), other.as_units())
    }

    /// Case-insensitive binary sort key (D8): byte-equal keys correspond to
    /// [`compare_ignore_case`](Utf16::compare_ignore_case) equality; the key's
    /// byte order is the invariant `LCMAP_SORTKEY` collation.
    #[must_use]
    pub fn sort_key(&self) -> Vec<u8> {
        windows_text_sys::sort_key(self.as_units())
    }
}

/// Test/off-Windows ordinal casing that folds **ASCII** `a-z` to `A-Z` only.
///
/// This is a reference stand-in so that case-insensitive logic can be exercised
/// without FFI. It is exposed only under `#[cfg(test)]` or the `testing`
/// feature and **must never** be used as the production comparator: the
/// production implementation is [`Win32OrdinalCasing`], which folds the full
/// Unicode range per the OS.
#[cfg(any(test, feature = "testing"))]
#[derive(Debug, Clone, Copy, Default)]
pub struct AsciiOrdinalCasing;

#[cfg(any(test, feature = "testing"))]
impl AsciiOrdinalCasing {
    fn fold(u: u16) -> u16 {
        if (b'a' as u16..=b'z' as u16).contains(&u) {
            u - 32
        } else {
            u
        }
    }
}

#[cfg(any(test, feature = "testing"))]
impl OrdinalCasing for AsciiOrdinalCasing {
    fn compare_ignore_case(&self, a: &[u16], b: &[u16]) -> Ordering {
        a.iter()
            .map(|&u| Self::fold(u))
            .cmp(b.iter().map(|&u| Self::fold(u)))
    }

    fn sort_key(&self, s: &[u16]) -> Vec<u8> {
        units_to_be_bytes(&s.iter().map(|&u| Self::fold(u)).collect::<Vec<_>>())
    }
}
