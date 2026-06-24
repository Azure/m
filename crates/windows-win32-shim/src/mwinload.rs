// Copyright (c) Microsoft Corporation.

//! The Win32 dynamic-loader C ABI (`mLoadLibrary*` / `mGetProcAddress` /
//! `mFreeLibrary` / `mGetModuleHandle*`) — MW9.
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

use windows_sys::Win32::Foundation::{BOOL, FARPROC, FreeLibrary, HANDLE, HMODULE, TRUE};
use windows_sys::Win32::System::LibraryLoader::{
    GetModuleHandleA, GetModuleHandleExA, GetModuleHandleExW, GetModuleHandleW,
    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, GET_MODULE_HANDLE_EX_FLAG_PIN, GetProcAddress,
    LoadLibraryA, LoadLibraryExA, LoadLibraryExW, LoadLibraryW, LOAD_LIBRARY_FLAGS,
};

use crate::ansi;
use crate::loader::{
    FreeDisposition, LoadDisposition, ModuleHandleDisposition, ProcDisposition, ProcQuery,
    RawModule, ShimProc,
};
use crate::session::session;

/// A `GetProcAddress` `lpProcName` whose value fits in this many low bits is an
/// ordinal (`MAKEINTRESOURCE`), not a string pointer: the high word is zero.
const ORDINAL_NAME_SHIFT: u32 = 16;
/// Mask selecting the ordinal value out of an `lpProcName` that is an ordinal.
const ORDINAL_VALUE_MASK: usize = 0xffff;

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

/// Decode an `lpProcName` argument into a [`ProcQuery`]. When the pointer value
/// has a zero high word it is an ordinal (`MAKEINTRESOURCE`); otherwise it is a
/// NUL-terminated `CP_ACP` export name decoded via the `ansi` boundary.
///
/// # Safety
///
/// When `p` is a string pointer (high word non-zero) it must be NUL-terminated.
unsafe fn read_proc_query(p: *const u8) -> ProcQuery {
    let value = p as usize;
    if value >> ORDINAL_NAME_SHIFT == 0 {
        return ProcQuery::Ordinal((value & ORDINAL_VALUE_MASK) as u16);
    }
    // SAFETY: a non-ordinal lpProcName is a NUL-terminated export name.
    ProcQuery::Named(unsafe { read_ansi_string(p) })
}

/// Convert a shim proc address into the Win32 `FARPROC` return type. A
/// [`ShimProc`] of `0` becomes the null "not found" result.
fn shim_proc_to_farproc(proc: ShimProc) -> FARPROC {
    // SAFETY: FARPROC is `Option<unsafe extern "system" fn() -> isize>`, a
    // nullable pointer-sized value; transmuting a code address (or 0 for null)
    // to it is the standard FARPROC round-trip.
    unsafe { core::mem::transmute::<usize, FARPROC>(proc.0) }
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

/// `GetProcAddress`: resolve `lp_proc_name` against `h_module`. A shimmed proc
/// name (substitute mode) or an engine-supplied sentinel proc returns the shim
/// body without touching the OS; every other case forwards to the real
/// `GetProcAddress`. Off mode is a pure passthrough (SHIM-D16).
#[unsafe(no_mangle)]
pub extern "system" fn mGetProcAddress(h_module: HMODULE, lp_proc_name: *const u8) -> FARPROC {
    let module = h_module as RawModule;
    // SAFETY: lp_proc_name is an ordinal or a NUL-terminated name (C ABI).
    let query = unsafe { read_proc_query(lp_proc_name) };
    match session().with_loader(|loader| loader.on_get_proc_address(module, &query)) {
        ProcDisposition::Shim(proc) => shim_proc_to_farproc(proc),
        // SAFETY: forwarding the caller's original handle and pointer to the
        // real resolver against a genuine module.
        ProcDisposition::Forward => unsafe { GetProcAddress(h_module, lp_proc_name) },
    }
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

/// `GetModuleHandleW`: resolve a previously-minted engine sentinel by name (no
/// OS call), else forward to the real `GetModuleHandleW`. A null name (the
/// caller's own module) is never a sentinel and forwards (SHIM-D16).
#[unsafe(no_mangle)]
pub extern "system" fn mGetModuleHandleW(lp_module_name: *const u16) -> HMODULE {
    // SAFETY: lp_module_name is null or a NUL-terminated wide string (C ABI).
    let name = unsafe { read_wide_string(lp_module_name) };
    match session().with_loader(|loader| loader.on_get_module_handle(&name)) {
        ModuleHandleDisposition::Sentinel(raw) => raw as HMODULE,
        // SAFETY: forwarding the caller's original pointer to the real resolver.
        ModuleHandleDisposition::Forward => unsafe { GetModuleHandleW(lp_module_name) },
    }
}

/// `GetModuleHandleA`: the `CP_ACP` form of [`mGetModuleHandleW`].
#[unsafe(no_mangle)]
pub extern "system" fn mGetModuleHandleA(lp_module_name: *const u8) -> HMODULE {
    // SAFETY: lp_module_name is null or a NUL-terminated CP_ACP string (C ABI).
    let name = unsafe { read_ansi_string(lp_module_name) };
    match session().with_loader(|loader| loader.on_get_module_handle(&name)) {
        ModuleHandleDisposition::Sentinel(raw) => raw as HMODULE,
        // SAFETY: forwarding the caller's original pointer to the real resolver.
        ModuleHandleDisposition::Forward => unsafe { GetModuleHandleA(lp_module_name) },
    }
}

/// `GetModuleHandleExW`: the flags form of [`mGetModuleHandleW`]. A sentinel
/// match writes `ph_module` and returns `TRUE`; the `PIN` flag pins the sentinel
/// so it survives a later `mFreeLibrary` (minimal flag modeling, SHIM-D16). A
/// `FROM_ADDRESS` query carries an address rather than a name and is forwarded
/// verbatim, as an address cannot be mapped back to a sentinel.
#[unsafe(no_mangle)]
pub extern "system" fn mGetModuleHandleExW(
    dw_flags: u32,
    lp_module_name: *const u16,
    ph_module: *mut HMODULE,
) -> BOOL {
    if dw_flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS != 0 {
        // SAFETY: forwarding the caller's verbatim arguments to the real API.
        return unsafe { GetModuleHandleExW(dw_flags, lp_module_name, ph_module) };
    }
    // SAFETY: lp_module_name is null or a NUL-terminated wide string (C ABI).
    let name = unsafe { read_wide_string(lp_module_name) };
    let pin = dw_flags & GET_MODULE_HANDLE_EX_FLAG_PIN != 0;
    match session().with_loader(|loader| loader.on_get_module_handle_ex(&name, pin)) {
        ModuleHandleDisposition::Sentinel(raw) => {
            if !ph_module.is_null() {
                // SAFETY: ph_module is a caller-owned out-pointer (non-null).
                unsafe { *ph_module = raw as HMODULE };
            }
            TRUE
        }
        // SAFETY: forwarding the caller's verbatim arguments to the real API.
        ModuleHandleDisposition::Forward => unsafe {
            GetModuleHandleExW(dw_flags, lp_module_name, ph_module)
        },
    }
}

/// `GetModuleHandleExA`: the `CP_ACP` form of [`mGetModuleHandleExW`].
#[unsafe(no_mangle)]
pub extern "system" fn mGetModuleHandleExA(
    dw_flags: u32,
    lp_module_name: *const u8,
    ph_module: *mut HMODULE,
) -> BOOL {
    if dw_flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS != 0 {
        // SAFETY: forwarding the caller's verbatim arguments to the real API.
        return unsafe { GetModuleHandleExA(dw_flags, lp_module_name, ph_module) };
    }
    // SAFETY: lp_module_name is null or a NUL-terminated CP_ACP string (C ABI).
    let name = unsafe { read_ansi_string(lp_module_name) };
    let pin = dw_flags & GET_MODULE_HANDLE_EX_FLAG_PIN != 0;
    match session().with_loader(|loader| loader.on_get_module_handle_ex(&name, pin)) {
        ModuleHandleDisposition::Sentinel(raw) => {
            if !ph_module.is_null() {
                // SAFETY: ph_module is a caller-owned out-pointer (non-null).
                unsafe { *ph_module = raw as HMODULE };
            }
            TRUE
        }
        // SAFETY: forwarding the caller's verbatim arguments to the real API.
        ModuleHandleDisposition::Forward => unsafe {
            GetModuleHandleExA(dw_flags, lp_module_name, ph_module)
        },
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

    #[test]
    fn get_proc_address_forwards_to_real_resolver_in_off_mode() {
        // Off mode is a pure passthrough: resolving a genuine kernel32 export
        // returns the real address, and a missing export returns null.
        let name = wide("kernel32.dll");
        let handle = mLoadLibraryW(name.as_ptr());
        assert!(!handle.is_null());

        let proc = mGetProcAddress(handle, c"GetCurrentProcessId".as_ptr().cast());
        assert!(proc.is_some());

        let missing = mGetProcAddress(handle, c"NoSuchExport_zzz".as_ptr().cast());
        assert!(missing.is_none());

        assert_eq!(mFreeLibrary(handle), TRUE);
    }

    #[test]
    fn read_proc_query_classifies_ordinal_and_name() {
        // A small integer pointer value is an ordinal, never dereferenced.
        // SAFETY: the ordinal branch reads no memory.
        let ordinal = unsafe { read_proc_query(7 as *const u8) };
        assert_eq!(ordinal, ProcQuery::Ordinal(7));

        // SAFETY: a genuine NUL-terminated name is decoded as a named query.
        let named = unsafe { read_proc_query(c"RegOpenKeyExW".as_ptr().cast()) };
        assert_eq!(named, ProcQuery::Named("RegOpenKeyExW".to_owned()));
    }

    #[test]
    fn get_module_handle_w_forwards_to_real_resolver_in_off_mode() {
        // Off mode forwards: kernel32 is mapped, so a by-name query returns a
        // genuine handle; an unloaded module returns null.
        let name = wide("kernel32.dll");
        let handle = mGetModuleHandleW(name.as_ptr());
        assert!(!handle.is_null());

        let absent = wide("definitely_not_loaded_zzz.dll");
        assert!(mGetModuleHandleW(absent.as_ptr()).is_null());
    }

    #[test]
    fn get_module_handle_a_forwards_to_real_resolver_in_off_mode() {
        let handle = mGetModuleHandleA(c"kernel32.dll".as_ptr().cast());
        assert!(!handle.is_null());
    }

    #[test]
    fn get_module_handle_ex_w_forwards_to_real_resolver_in_off_mode() {
        let name = wide("kernel32.dll");
        let mut module: HMODULE = core::ptr::null_mut();
        let ok = mGetModuleHandleExW(0, name.as_ptr(), &mut module);
        assert_eq!(ok, TRUE);
        assert!(!module.is_null());
    }
}
