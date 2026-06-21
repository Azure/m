// Copyright (c) Microsoft Corporation.

//! The ordinal-casing seam (D6/D8): the [`OrdinalCasing`] trait, the Windows
//! production implementation, and a feature-gated ASCII reference.

use core::cmp::Ordering;

use crate::utf16::Utf16;

/// The ordinal-casing seam (D6/D8).
///
/// Provides Windows **ordinal** (never linguistic) case-insensitive comparison
/// and a binary sort key over UTF-16 code units. The sort key must reproduce,
/// under a plain byte comparison, the same equality and ordering that
/// [`compare_ignore_case`](OrdinalCasing::compare_ignore_case) yields directly.
///
/// This trait is the dependency-injection seam that lets downstream crates
/// unit-test case-insensitive logic **off Windows** by substituting the
/// [`AsciiOrdinalCasing`] reference for the Win32 implementation.
pub trait OrdinalCasing {
    /// Ordinal case-insensitive comparison of two code-unit sequences (D6).
    fn compare_ignore_case(&self, a: &[u16], b: &[u16]) -> Ordering;

    /// Opaque binary sort key for ordinal case-insensitive ordering/equality
    /// via byte comparison (D8).
    fn sort_key(&self, s: &[u16]) -> Vec<u8>;
}

/// Serialize folded UTF-16 code units big-endian so that a plain byte
/// comparison reproduces code-unit ordering.
fn units_to_be_bytes(units: &[u16]) -> Vec<u8> {
    let mut out = Vec::with_capacity(units.len() * 2);
    for &u in units {
        out.extend_from_slice(&u.to_be_bytes());
    }
    out
}

/// The mandated Windows production casing (D6/D8).
///
/// `compare_ignore_case` calls `CompareStringOrdinal`; `sort_key` ordinally
/// upper-cases via `LCMapStringEx`/`LCMAP_UPPERCASE` and serializes the result
/// big-endian. Both delegate to the [`windows-text-sys`](windows_text_sys) leaf,
/// where all `unsafe` is confined.
#[cfg(windows)]
#[derive(Debug, Clone, Copy, Default)]
pub struct Win32OrdinalCasing;

#[cfg(windows)]
impl OrdinalCasing for Win32OrdinalCasing {
    fn compare_ignore_case(&self, a: &[u16], b: &[u16]) -> Ordering {
        windows_text_sys::compare_ordinal_ignore_case(a, b)
    }

    fn sort_key(&self, s: &[u16]) -> Vec<u8> {
        units_to_be_bytes(&windows_text_sys::ordinal_upcase(s))
    }
}

#[cfg(windows)]
impl Utf16 {
    /// Ordinal case-insensitive comparison against another [`Utf16`] (D6).
    #[must_use]
    pub fn compare_ignore_case(&self, other: &Utf16) -> Ordering {
        windows_text_sys::compare_ordinal_ignore_case(self.as_units(), other.as_units())
    }

    /// Ordinal binary sort key (D8): byte comparison of two keys reproduces
    /// [`compare_ignore_case`](Utf16::compare_ignore_case).
    #[must_use]
    pub fn sort_key(&self) -> Vec<u8> {
        units_to_be_bytes(&windows_text_sys::ordinal_upcase(self.as_units()))
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
