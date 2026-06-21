// Copyright (c) Microsoft Corporation.

//! Windows implementation of the safe FFI wrappers. Every `unsafe` in the
//! `windows-text` effort lives in this module.

use core::cmp::Ordering;

use windows::Win32::Foundation::{GetLastError, LPARAM};
use windows::Win32::Globalization::{
    CSTR_EQUAL, CSTR_GREATER_THAN, CSTR_LESS_THAN, CompareStringOrdinal, LCMAP_UPPERCASE,
    LCMapStringEx, LOCALE_NAME_INVARIANT, MB_ERR_INVALID_CHARS, MULTI_BYTE_TO_WIDE_CHAR_FLAGS,
    MultiByteToWideChar, WideCharToMultiByte,
};
use windows::core::PCSTR;

/// Win32 code-page identifier for UTF-8. Used to opt into strict (no
/// best-fit / no invalid-sequence) decoding; see [`mb_to_wide`].
const CP_UTF8: u32 = 65001;

/// No extra `MultiByteToWideChar` flags (non-UTF-8 code pages).
const NO_MB_FLAGS: MULTI_BYTE_TO_WIDE_CHAR_FLAGS = MULTI_BYTE_TO_WIDE_CHAR_FLAGS(0);

/// No extra `WideCharToMultiByte` flags.
const NO_WC_FLAGS: u32 = 0;

/// A failed Win32 call, carrying the `GetLastError` code.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Win32Error(pub u32);

impl core::fmt::Display for Win32Error {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(f, "Win32 error 0x{:08X}", self.0)
    }
}

impl std::error::Error for Win32Error {}

fn last_error() -> Win32Error {
    Win32Error(unsafe { GetLastError().0 })
}

/// Ordinal, case-insensitive comparison of two UTF-16 code-unit sequences via
/// `CompareStringOrdinal(bIgnoreCase = TRUE)` (D6). Operates on code units, so
/// ill-formed UTF-16 is compared without panicking (D9).
///
/// # Panics
///
/// Panics only if `CompareStringOrdinal` reports failure, which cannot happen
/// for the valid (non-empty) slices passed here — the empty cases are handled
/// without calling Win32.
#[must_use]
pub fn compare_ordinal_ignore_case(a: &[u16], b: &[u16]) -> Ordering {
    match (a.is_empty(), b.is_empty()) {
        (true, true) => return Ordering::Equal,
        (true, false) => return Ordering::Less,
        (false, true) => return Ordering::Greater,
        (false, false) => {}
    }

    let result = unsafe { CompareStringOrdinal(a, b, true) };
    if result == CSTR_LESS_THAN {
        Ordering::Less
    } else if result == CSTR_GREATER_THAN {
        Ordering::Greater
    } else if result == CSTR_EQUAL {
        Ordering::Equal
    } else {
        panic!("CompareStringOrdinal failed: {}", last_error());
    }
}

/// Ordinal upper-casing of UTF-16 code units via
/// `LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE)`.
///
/// This is the case-fold primitive the safe crate serializes (big-endian) into
/// an ordinal binary sort key (D8). Empty input maps to empty output without
/// calling Win32.
///
/// # Panics
///
/// Panics only if `LCMapStringEx` reports failure, which cannot happen for the
/// valid (non-empty) slice passed here.
#[must_use]
pub fn ordinal_upcase(s: &[u16]) -> Vec<u16> {
    if s.is_empty() {
        return Vec::new();
    }

    let needed =
        unsafe { LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, s, None, None, None, LPARAM(0)) };
    if needed <= 0 {
        panic!("LCMapStringEx sizing failed: {}", last_error());
    }

    let mut buf = vec![0u16; needed as usize];
    let written = unsafe {
        LCMapStringEx(
            LOCALE_NAME_INVARIANT,
            LCMAP_UPPERCASE,
            s,
            Some(&mut buf),
            None,
            None,
            LPARAM(0),
        )
    };
    if written <= 0 {
        panic!("LCMapStringEx mapping failed: {}", last_error());
    }
    buf.truncate(written as usize);
    buf
}

/// Decode bytes in `code_page` to UTF-16 via `MultiByteToWideChar`.
///
/// For `CP_UTF8` (65001) the strict `MB_ERR_INVALID_CHARS` flag is set, so
/// malformed UTF-8 is rejected rather than silently substituted. Empty input
/// decodes to an empty vector. Returns [`Win32Error`] on failure.
///
/// # Errors
///
/// Returns the `GetLastError` code if the OS rejects the byte sequence (for
/// example, `ERROR_NO_UNICODE_TRANSLATION` for invalid UTF-8).
pub fn mb_to_wide(code_page: u32, bytes: &[u8]) -> Result<Vec<u16>, Win32Error> {
    if bytes.is_empty() {
        return Ok(Vec::new());
    }

    let flags = if code_page == CP_UTF8 {
        MB_ERR_INVALID_CHARS
    } else {
        NO_MB_FLAGS
    };

    let needed = unsafe { MultiByteToWideChar(code_page, flags, bytes, None) };
    if needed <= 0 {
        return Err(last_error());
    }

    let mut buf = vec![0u16; needed as usize];
    let written = unsafe { MultiByteToWideChar(code_page, flags, bytes, Some(&mut buf)) };
    if written <= 0 {
        return Err(last_error());
    }
    buf.truncate(written as usize);
    Ok(buf)
}

/// Encode UTF-16 code units to bytes in `code_page` via `WideCharToMultiByte`.
///
/// Empty input encodes to an empty vector. Returns [`Win32Error`] on failure.
///
/// # Errors
///
/// Returns the `GetLastError` code if the OS cannot encode the units in the
/// target code page.
pub fn wide_to_mb(code_page: u32, units: &[u16]) -> Result<Vec<u8>, Win32Error> {
    if units.is_empty() {
        return Ok(Vec::new());
    }

    let needed =
        unsafe { WideCharToMultiByte(code_page, NO_WC_FLAGS, units, None, PCSTR::null(), None) };
    if needed <= 0 {
        return Err(last_error());
    }

    let mut buf = vec![0u8; needed as usize];
    let written = unsafe {
        WideCharToMultiByte(
            code_page,
            NO_WC_FLAGS,
            units,
            Some(&mut buf),
            PCSTR::null(),
            None,
        )
    };
    if written <= 0 {
        return Err(last_error());
    }
    buf.truncate(written as usize);
    Ok(buf)
}
