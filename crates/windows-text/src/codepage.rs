// Copyright (c) Microsoft Corporation.

//! Code-page conversions (D16 charter), porting `m::windows_strings::convert`.
//!
//! Windows-only: these delegate to the [`windows-text-sys`](windows_text_sys)
//! leaf's `MultiByteToWideChar` / `WideCharToMultiByte` wrappers.

use crate::error::{Error, Result};
use crate::utf16::Utf16;

/// Well-known Win32 code-page identifiers. Changing any of these values is a
/// breaking change (they are fixed Windows protocol constants).
mod id {
    /// The system ANSI code page (`CP_ACP`).
    pub const ANSI: u32 = 0;
    /// The system OEM code page (`CP_OEMCP`).
    pub const OEM: u32 = 1;
    /// UTF-8 (`CP_UTF8`).
    pub const UTF8: u32 = 65001;
}

/// A Windows code page used to transcode between bytes and UTF-16.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CodePage(u32);

impl CodePage {
    /// The system ANSI code page (`CP_ACP`).
    pub const ANSI: CodePage = CodePage(id::ANSI);
    /// The system OEM code page (`CP_OEMCP`).
    pub const OEM: CodePage = CodePage(id::OEM);
    /// UTF-8 (`CP_UTF8`).
    pub const UTF8: CodePage = CodePage(id::UTF8);

    /// Construct a code page from an explicit numeric identifier (for example,
    /// `1252` for Windows-1252).
    #[must_use]
    pub const fn from_id(id: u32) -> Self {
        Self(id)
    }

    /// The numeric code-page identifier.
    #[must_use]
    pub const fn id(self) -> u32 {
        self.0
    }
}

impl Utf16 {
    /// Decode `bytes`, interpreted in code page `cp`, into UTF-16.
    ///
    /// UTF-8 input is decoded strictly (malformed sequences are rejected).
    ///
    /// # Errors
    ///
    /// Returns [`Error::CodePage`] if the OS rejects the byte sequence.
    pub fn from_code_page(cp: CodePage, bytes: &[u8]) -> Result<Self> {
        windows_text_sys::mb_to_wide(cp.id(), bytes)
            .map(Self::from_units)
            .map_err(|e| Error::CodePage {
                code_page: cp.id(),
                win32: e.0,
            })
    }

    /// Encode this string's UTF-16 code units into `bytes` in code page `cp`.
    ///
    /// # Errors
    ///
    /// Returns [`Error::CodePage`] if the units cannot be encoded in `cp`.
    pub fn to_code_page(&self, cp: CodePage) -> Result<Vec<u8>> {
        windows_text_sys::wide_to_mb(cp.id(), self.as_units()).map_err(|e| Error::CodePage {
            code_page: cp.id(),
            win32: e.0,
        })
    }
}
