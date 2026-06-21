// Copyright (c) Microsoft Corporation.

//! Safe, reusable Windows string layer.
//!
//! `windows-text` is the Rust home for Windows string handling shared across
//! the platform-isolation effort and beyond (charter: D16 in
//! `windows-platform-isolation/DESIGN-NOTES.md`). It is unconditionally
//! `#![forbid(unsafe_code)]`; every `unsafe` Win32 call lives in the
//! [`windows-text-sys`](windows_text_sys) leaf.
//!
//! Committed scope:
//!
//! * [`Utf16`] — an owned UTF-16 string shaped after `std::basic_string<char16_t>`.
//!   UTF-8 ingress, lossless `u16` storage (ill-formed sequences preserved),
//!   fallible UTF-8 egress (D7/D9).
//! * Ordinal casing — Windows **ordinal** (never linguistic) case-insensitive
//!   comparison and a consistent binary [`sort_key`](Utf16::sort_key) (D6/D8),
//!   surfaced both as inherent methods on [`Utf16`] (Windows only) and through
//!   the [`OrdinalCasing`] dependency-injection trait.
//! * Code pages — [`CodePage`] conversions over `MultiByteToWideChar` /
//!   `WideCharToMultiByte` (Windows only).
//!
//! UTF-32 and other exotic transcoding are deliberately out of scope for now.

#![forbid(unsafe_code)]

mod casing;
mod error;
mod utf16;

#[cfg(windows)]
mod codepage;

pub use casing::OrdinalCasing;
pub use error::{Error, Result};
pub use utf16::Utf16;

#[cfg(windows)]
pub use casing::Win32OrdinalCasing;

#[cfg(windows)]
pub use codepage::CodePage;

#[cfg(any(test, feature = "testing"))]
pub use casing::AsciiOrdinalCasing;

#[cfg(test)]
mod tests;
