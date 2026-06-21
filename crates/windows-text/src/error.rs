// Copyright (c) Microsoft Corporation.

//! The crate error type.

use core::fmt;

/// Errors produced by `windows-text`.
#[derive(Debug, Clone, PartialEq, Eq)]
#[non_exhaustive]
pub enum Error {
    /// Stored UTF-16 was not well-formed at UTF-8 egress (D9). The raw code
    /// units are still preserved losslessly by the [`Utf16`](crate::Utf16) that
    /// produced this error.
    IllFormedUtf16,
    /// A Win32 code-page conversion failed.
    CodePage {
        /// The code-page identifier the conversion targeted.
        code_page: u32,
        /// The underlying `GetLastError` value.
        win32: u32,
    },
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::IllFormedUtf16 => f.write_str("UTF-16 string is not well-formed"),
            Error::CodePage { code_page, win32 } => write!(
                f,
                "code-page {code_page} conversion failed (Win32 error 0x{win32:08X})"
            ),
        }
    }
}

impl std::error::Error for Error {}

/// Convenience alias for results in this crate.
pub type Result<T> = core::result::Result<T, Error>;
