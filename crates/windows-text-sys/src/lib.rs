// Copyright (c) Microsoft Corporation.

//! Unsafe FFI leaf for the `windows-text` effort.
//!
//! This is the **only** crate in the effort that contains `unsafe` (D13 /
//! Option B in `windows-platform-isolation/DESIGN-NOTES.md`). It wraps the
//! buffer-management-critical Win32 string primitives —
//! [`CompareStringOrdinal`], [`LCMapStringEx`], [`MultiByteToWideChar`], and
//! [`WideCharToMultiByte`] — as **safe**, slice-in / owned-out functions. No
//! raw pointers cross this crate's boundary; every two-call length-probe and
//! `GetLastError` mapping is confined here.
//!
//! All higher-level types (the `Utf16` string, `CodePage`, error mapping) live
//! in the safe `windows-text` crate, which is unconditionally
//! `#![forbid(unsafe_code)]`.
//!
//! [`CompareStringOrdinal`]: https://learn.microsoft.com/windows/win32/api/stringapiset/nf-stringapiset-comparestringordinal
//! [`LCMapStringEx`]: https://learn.microsoft.com/windows/win32/api/winnls/nf-winnls-lcmapstringex
//! [`MultiByteToWideChar`]: https://learn.microsoft.com/windows/win32/api/stringapiset/nf-stringapiset-multibytetowidechar
//! [`WideCharToMultiByte`]: https://learn.microsoft.com/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte

#[cfg(windows)]
mod win32;

#[cfg(windows)]
pub use win32::{
    Win32Error, compare_ordinal_ignore_case, mb_to_wide, ordinal_upcase, wide_to_mb,
};
