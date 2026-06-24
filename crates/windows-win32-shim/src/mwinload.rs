// Copyright (c) Microsoft Corporation.

//! The Win32 dynamic-loader C ABI (`mLoadLibrary*` / `mFreeLibrary`) — MW9.
//!
//! These entry points mirror the Win32 loader prototypes so the shim is a
//! drop-in for the loader half of the C++ `mwin32` surface. Each body does only
//! the ABI marshaling: decode the caller's module name, ask the safe
//! [`LoaderState`](crate::loader::LoaderState) policy how the call should behave
//! (SHIM-D16), and either return a minted sentinel or forward to the real OS
//! loader and intern the result. The substitution / observation logic lives in
//! [`crate::loader`]; this module holds no policy.
//!
//! Per SHIM-D2 this is the only place raw caller pointers and raw `HMODULE`
//! values are touched, so the module opts back into `unsafe_code` (the crate
//! root denies it). The two FFI-boundary clippy lints are allowed module-wide
//! for the same reasons as [`crate::mwinfile`]:
//! - `not_unsafe_ptr_arg_deref`: every entry point is a C export, not a Rust API;
//! - `too_many_arguments`: the argument lists are fixed by the Win32 prototypes.
//!
//! ## Transparency (SHIM-D16)
//!
//! Any `HMODULE` this layer did not mint is forwarded untouched — `mFreeLibrary`
//! of a real module calls the real `FreeLibrary`, and in [`LoaderMode::Off`] a
//! load is a pure passthrough that records nothing. The `*A` forms decide on the
//! `CP_ACP`-decoded name (SHIM-D15) but forward the **original** pointer to the
//! real `*A` loader, so a forwarded call is byte-for-byte what the host issued.

#![allow(unsafe_code)]
#![allow(clippy::not_unsafe_ptr_arg_deref, clippy::too_many_arguments)]

use windows_sys::Win32::Foundation::{BOOL, FreeLibrary, HANDLE, HMODULE, TRUE};
use windows_sys::Win32::System::LibraryLoader::{
    LoadLibraryA, LoadLibraryExA, LoadLibraryExW, LoadLibraryW, LOAD_LIBRARY_FLAGS,
};

use crate::ansi;
use crate::loader::{FreeDisposition, LoadDisposition, RawModule};
use crate::session::session;

// --- Pointer marshaling helpers ---------------------------------------------

/// Decode a NUL-terminated wide module name into an owned `String` for policy
/// lookup and observation. A null pointer yields the empty string.
///
/// # Safety
///
/// `p` must be null or point to a NUL-terminated sequence of `u16`.
unsafe fn read_wide_string(p: *const u16) -> String {
    if p.is_null() {
        return String::new();
    }
    // SAFETY: caller guarantees a NUL-terminated buffer; we stop at the NUL and
    // never read past it.
    unsafe {
        let mut len = 0usize;
        while *p.add(len) != 0 {
            len += 1;
        }
        String::from_utf16_lossy(core::slice::from_raw_parts(p, len))
    }
}

/// Decode a NUL-terminated `CP_ACP` (`*A`) module name into an owned `String`
/// via the `ansi` boundary (SHIM-D15). A null pointer yields the empty string.
///
/// # Safety
///
/// `p` must be null or point to a NUL-terminated sequence of bytes.
unsafe fn read_ansi_string(p: *const u8) -> String {
    if p.is_null() {
        return String::new();
    }
    // SAFETY: caller guarantees a NUL-terminated buffer; we stop at the NUL and
    // never read past it.
    unsafe {
        let mut len = 0usize;
        while *p.add(len) != 0 {
            len += 1;
        }
        let bytes = core::slice::from_raw_parts(p, len);
        String::from_utf16_lossy(ansi::ansi_to_utf16(bytes).as_units())
    }
}

/// Run the loader policy for a `LoadLibrary*` of `name`, calling `forward` to
/// reach the real OS loader when the policy does not substitute the module.
fn dispatch_load(name: &str, forward: impl FnOnce() -> HMODULE) -> HMODULE {
    let s = session();
    match s.with_loader(|loader| loader.on_load_library(name)) {
        LoadDisposition::Substitute(raw) => raw as HMODULE,
        LoadDisposition::Forward { record } => {
            let real = forward();
            if record && !real.is_null() {
                s.with_loader(|loader| loader.record_loaded(real as RawModule));
            }
            real
        }
    }
}

// --- Loader entry points (MW9-2) --------------------------------------------

/// `LoadLibraryW`: load (or substitute) the module named by `lp_lib_file_name`.
/// A substituted engine returns a minted sentinel `HMODULE`; every other load
/// forwards to the real loader and is interned. Returns null on loader failure.
#[unsafe(no_mangle)]
pub extern "system" fn mLoadLibraryW(lp_lib_file_name: *const u16) -> HMODULE {
    // SAFETY: lp_lib_file_name is null or a NUL-terminated wide string (C ABI).
    let name = unsafe { read_wide_string(lp_lib_file_name) };
    // SAFETY: forwarding the caller's original pointer to the real loader.
    dispatch_load(&name, || unsafe { LoadLibraryW(lp_lib_file_name) })
}

/// `LoadLibraryExW`: the flags/`hFile` form of [`mLoadLibraryW`]. The reserved
/// `h_file` and `dw_flags` are forwarded verbatim to the real loader.
#[unsafe(no_mangle)]
pub extern "system" fn mLoadLibraryExW(
    lp_lib_file_name: *const u16,
    h_file: HANDLE,
    dw_flags: LOAD_LIBRARY_FLAGS,
) -> HMODULE {
    // SAFETY: lp_lib_file_name is null or a NUL-terminated wide string (C ABI).
    let name = unsafe { read_wide_string(lp_lib_file_name) };
    // SAFETY: forwarding the caller's original pointer and flags to the loader.
    dispatch_load(&name, || unsafe {
        LoadLibraryExW(lp_lib_file_name, h_file, dw_flags)
    })
}

/// `LoadLibraryA`: the `CP_ACP` form of [`mLoadLibraryW`]. The policy decides on
/// the decoded name, but a forwarded call passes the caller's original pointer
/// to the real `LoadLibraryA`.
#[unsafe(no_mangle)]
pub extern "system" fn mLoadLibraryA(lp_lib_file_name: *const u8) -> HMODULE {
    // SAFETY: lp_lib_file_name is null or a NUL-terminated CP_ACP string (C ABI).
    let name = unsafe { read_ansi_string(lp_lib_file_name) };
    // SAFETY: forwarding the caller's original pointer to the real loader.
    dispatch_load(&name, || unsafe { LoadLibraryA(lp_lib_file_name) })
}

/// `LoadLibraryExA`: the `CP_ACP` form of [`mLoadLibraryExW`].
#[unsafe(no_mangle)]
pub extern "system" fn mLoadLibraryExA(
    lp_lib_file_name: *const u8,
    h_file: HANDLE,
    dw_flags: LOAD_LIBRARY_FLAGS,
) -> HMODULE {
    // SAFETY: lp_lib_file_name is null or a NUL-terminated CP_ACP string (C ABI).
    let name = unsafe { read_ansi_string(lp_lib_file_name) };
    // SAFETY: forwarding the caller's original pointer and flags to the loader.
    dispatch_load(&name, || unsafe {
        LoadLibraryExA(lp_lib_file_name, h_file, dw_flags)
    })
}

/// `FreeLibrary`: release a minted sentinel (no OS call) or forward any genuine
/// `HMODULE` to the real `FreeLibrary` (SHIM-D16 transparency invariant).
#[unsafe(no_mangle)]
pub extern "system" fn mFreeLibrary(h_lib_module: HMODULE) -> BOOL {
    let module = h_lib_module as RawModule;
    match session().with_loader(|loader| loader.on_free_library(module)) {
        FreeDisposition::Released => TRUE,
        // SAFETY: a non-minted value is a genuine OS module; FreeLibrary accepts
        // any handle value and reports its own failure.
        FreeDisposition::Forward => unsafe { FreeLibrary(h_lib_module) },
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Build a NUL-terminated wide string for a `*W` entry-point argument.
    fn wide(s: &str) -> Vec<u16> {
        s.encode_utf16().chain(core::iter::once(0)).collect()
    }

    #[test]
    fn load_library_w_forwards_to_real_loader_in_off_mode() {
        // The process-wide session defaults to LoaderMode::Off, so this is a pure
        // passthrough to the OS loader. kernel32 is permanently mapped, so the
        // balanced load/free pair is safe.
        let name = wide("kernel32.dll");
        let handle = mLoadLibraryW(name.as_ptr());
        assert!(!handle.is_null());
        assert_eq!(mFreeLibrary(handle), TRUE);
    }

    #[test]
    fn load_library_a_forwards_to_real_loader_in_off_mode() {
        let handle = mLoadLibraryA(c"kernel32.dll".as_ptr().cast());
        assert!(!handle.is_null());
        assert_eq!(mFreeLibrary(handle), TRUE);
    }

    #[test]
    fn load_library_ex_w_forwards_to_real_loader_in_off_mode() {
        let name = wide("kernel32.dll");
        let handle = mLoadLibraryExW(name.as_ptr(), core::ptr::null_mut(), 0);
        assert!(!handle.is_null());
        assert_eq!(mFreeLibrary(handle), TRUE);
    }
}
