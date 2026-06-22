// Copyright (c) Microsoft Corporation.

//! Error type for the thread pool wrapper.
//!
//! Win32 thread pool entry points report failure either by returning a null
//! handle (with the reason in `GetLastError`) or, for the few that return
//! `BOOL`, by returning `FALSE`. We capture the OS error code into a small
//! owned error type rather than surfacing `windows-sys` types to callers
//! (the dependency is an implementation detail — see TP-D1).

use core::fmt;

/// An error returned by a thread pool operation.
///
/// Wraps the Win32 error code captured from `GetLastError` at the point of
/// failure. The code is owned by this crate; the `windows-sys` binding is not
/// exposed.
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct ThreadPoolError {
    code: u32,
}

impl ThreadPoolError {
    /// Construct from a raw Win32 error code.
    #[must_use]
    pub const fn from_code(code: u32) -> Self {
        Self { code }
    }

    /// Capture the calling thread's last Win32 error (`GetLastError`).
    #[must_use]
    pub fn last_os_error() -> Self {
        Self {
            code: crate::ffi::last_error_code(),
        }
    }

    /// The underlying Win32 error code.
    #[must_use]
    pub const fn code(self) -> u32 {
        self.code
    }
}

impl fmt::Display for ThreadPoolError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "windows thread pool error (os error {})", self.code)
    }
}

impl fmt::Debug for ThreadPoolError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("ThreadPoolError")
            .field("code", &self.code)
            .finish()
    }
}

impl std::error::Error for ThreadPoolError {}

/// Result alias for thread pool operations.
pub type ThreadPoolResult<T> = Result<T, ThreadPoolError>;
