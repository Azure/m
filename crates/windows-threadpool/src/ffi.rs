// Copyright (c) Microsoft Corporation.

//! FFI quarantine (TP-D4).
//!
//! This is the **only** module in the crate permitted to use `unsafe`. It binds
//! the raw Win32 thread pool entry points through `windows-sys` (TP-D1) and
//! converts them into safe RAII wrappers and submit APIs consumed by the rest
//! of the crate. The `extern "system"` trampolines that bridge OS callbacks
//! back into boxed Rust closures (TP-D2) live here too.
//!
//! Keeping every `unsafe` line in one small, auditable place is the whole point
//! of the split; nothing above this module needs `unsafe`.

use windows_sys::Win32::Foundation::GetLastError;

/// The calling thread's last Win32 error code (`GetLastError`).
pub(crate) fn last_error_code() -> u32 {
    // SAFETY: `GetLastError` reads thread-local error state and has no
    // preconditions or side effects.
    unsafe { GetLastError() }
}
