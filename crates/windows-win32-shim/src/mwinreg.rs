// Copyright (c) Microsoft Corporation.

//! The Win32 registry C ABI (`mReg*W` entry points) — MW2.
//!
//! Each exported symbol mirrors a `windows-registry` `Reg*W` prototype so the
//! shim is a drop-in for the C++ `mwin32` registry surface. A body does only
//! three things: marshal raw caller pointers into owned Rust values, delegate to
//! the safe surface-generic core ([`crate::reg_ops`]) over the process-wide live
//! [`session`], and marshal results back out through the caller's buffers
//! following the Win32 buffer/size conventions.
//!
//! Per SHIM-D2 this is the only place raw caller pointers are touched, so the
//! module opts back into `unsafe_code` (the crate root denies it). The exported
//! functions take raw pointers and dereference them — that is the C ABI contract
//! (callers are C, never safe Rust), so the two FFI-boundary clippy lints below
//! are allowed module-wide rather than papered over per call:
//! - `not_unsafe_ptr_arg_deref`: every entry point is a C export, not a Rust API;
//! - `too_many_arguments`: the argument lists are fixed by the Win32 prototypes.

#![allow(unsafe_code)]
#![allow(clippy::not_unsafe_ptr_arg_deref, clippy::too_many_arguments)]

use core::ffi::c_void;
use core::ptr;

use windows_platform_isolation::{KeyPath, Utf16};
use windows_sys::Win32::Foundation::{
    ERROR_INVALID_PARAMETER, ERROR_MORE_DATA, ERROR_NO_MORE_ITEMS, ERROR_NOT_SUPPORTED,
    ERROR_SUCCESS, FILETIME,
};
use windows_sys::Win32::System::Registry::HKEY;

use crate::error_map::Lstatus;
use crate::reg_ops;
use crate::session::session;

// --- Named status constants (no manifest numeric literals in logic) ----------

/// `ERROR_SUCCESS` as an `LSTATUS`.
const SUCCESS: Lstatus = ERROR_SUCCESS as Lstatus;
/// `ERROR_INVALID_PARAMETER` as an `LSTATUS`.
const INVALID_PARAMETER: Lstatus = ERROR_INVALID_PARAMETER as Lstatus;
/// `ERROR_NOT_SUPPORTED` as an `LSTATUS`.
const NOT_SUPPORTED: Lstatus = ERROR_NOT_SUPPORTED as Lstatus;
/// `ERROR_NO_MORE_ITEMS` as an `LSTATUS`.
const NO_MORE_ITEMS: Lstatus = ERROR_NO_MORE_ITEMS as Lstatus;

/// Path separators accepted in a `lpSubKey` string.
const SEP_BACKSLASH: u16 = 0x5C; // '\'
const SEP_SLASH: u16 = 0x2F; // '/'

// --- Pointer marshaling helpers ---------------------------------------------

/// Read a NUL-terminated wide string into owned units (excluding the NUL). A
/// null pointer yields an empty vector.
///
/// # Safety
///
/// `p` must be null or point to a NUL-terminated sequence of `u16`.
unsafe fn read_wide_units(p: *const u16) -> Vec<u16> {
    if p.is_null() {
        return Vec::new();
    }
    // SAFETY: caller guarantees a NUL-terminated buffer; we stop at the NUL and
    // never read past it.
    unsafe {
        let mut len = 0usize;
        while *p.add(len) != 0 {
            len += 1;
        }
        ptr::slice_from_raw_parts(p, len)
            .as_ref()
            .map(<[u16]>::to_vec)
            .unwrap_or_default()
    }
}

/// Marshal a `lpValueName` pointer into a [`Utf16`] value name (empty for the
/// default value).
///
/// # Safety
///
/// `p` must be null or a NUL-terminated wide string.
unsafe fn value_name(p: *const u16) -> Utf16 {
    // SAFETY: forwarded contract.
    Utf16::from_units(unsafe { read_wide_units(p) })
}

/// Marshal a `lpSubKey` pointer into a [`KeyPath`], splitting on `\` or `/` and
/// dropping empty components.
///
/// # Safety
///
/// `p` must be null or a NUL-terminated wide string.
unsafe fn subkey_path(p: *const u16) -> KeyPath {
    // SAFETY: forwarded contract.
    let units = unsafe { read_wide_units(p) };
    let mut path = KeyPath::root();
    for component in units.split(|&u| u == SEP_BACKSLASH || u == SEP_SLASH) {
        if !component.is_empty() {
            path.push(Utf16::from_units(component.to_vec()));
        }
    }
    path
}

/// Write a wide name into a caller buffer following the `lpcch` character-count
/// in/out contract (used by `RegEnumKeyExW` / `RegEnumValueW`).
///
/// # Safety
///
/// `lp_name` must be null or valid for `*lpcch` `u16` writes; `lpcch` must be
/// null or valid for a `u32` read/write.
unsafe fn write_wide_name(units: &[u16], lp_name: *mut u16, lpcch: *mut u32) -> Lstatus {
    // `lpcch` is required and holds the buffer capacity in characters on entry.
    if lpcch.is_null() {
        return INVALID_PARAMETER;
    }
    // SAFETY: capacity read from the caller's in/out count pointer.
    let capacity = (unsafe { *lpcch }) as usize;
    let needed = units.len();
    if lp_name.is_null() || capacity < needed + 1 {
        // SAFETY: lpcch is non-null (checked).
        unsafe { *lpcch = needed as u32 };
        return ERROR_MORE_DATA as Lstatus;
    }
    // SAFETY: lp_name has room for needed + 1 units (checked above).
    unsafe {
        ptr::copy_nonoverlapping(units.as_ptr(), lp_name, needed);
        *lp_name.add(needed) = 0;
        *lpcch = needed as u32;
    }
    SUCCESS
}

/// Fill a caller value buffer (`RegQueryValueExW` / `RegGetValueW` /
/// `RegEnumValueW` data contract).
///
/// # Safety
///
/// `lp_type`/`lp_data`/`lpcb_data` must be null or valid for writes; when both
/// `lp_data` and `lpcb_data` are non-null, `lp_data` must point to at least
/// `*lpcb_data` writable bytes.
unsafe fn fill_value_buffer(
    bytes: &[u8],
    win32_type: u32,
    lp_type: *mut u32,
    lp_data: *mut u8,
    lpcb_data: *mut u32,
) -> Lstatus {
    let has_buffer = !lp_data.is_null();
    // SAFETY: lpcb_data read only when non-null.
    let capacity = if lpcb_data.is_null() {
        0
    } else {
        (unsafe { *lpcb_data }) as usize
    };
    let outcome = reg_ops::apply_query_buffer(bytes.len(), has_buffer, capacity);
    // SAFETY: each out pointer written only when non-null.
    unsafe {
        if !lp_type.is_null() {
            *lp_type = win32_type;
        }
        if !lpcb_data.is_null() {
            *lpcb_data = outcome.required as u32;
        }
        if outcome.copy {
            ptr::copy_nonoverlapping(bytes.as_ptr(), lp_data, bytes.len());
        }
    }
    outcome.status
}

// --- Implemented registry W entry points (MW2-1..MW2-3) ----------------------

/// `RegOpenKeyExW`: open an existing subkey of `h_key`.
#[unsafe(no_mangle)]
pub extern "system" fn mRegOpenKeyExW(
    h_key: HKEY,
    lp_sub_key: *const u16,
    ul_options: u32,
    _sam_desired: u32,
    phk_result: *mut HKEY,
) -> Lstatus {
    // SAFETY: phk_result written only when non-null.
    unsafe {
        if !phk_result.is_null() {
            *phk_result = ptr::null_mut();
        }
    }
    if ul_options != 0 {
        return INVALID_PARAMETER;
    }
    // SAFETY: lp_sub_key is a NUL-terminated wide string or null.
    let sub = unsafe { subkey_path(lp_sub_key) };
    let s = session();
    match s.with_registry(|reg| reg_ops::open_key(reg, s.handles(), h_key as usize, &sub)) {
        Ok(handle) => {
            // SAFETY: phk_result written only when non-null.
            unsafe {
                if !phk_result.is_null() {
                    *phk_result = handle as HKEY;
                }
            }
            SUCCESS
        }
        Err(code) => code,
    }
}

/// `RegCreateKeyExW`: create (or open) a subkey of `h_key`.
#[unsafe(no_mangle)]
pub extern "system" fn mRegCreateKeyExW(
    h_key: HKEY,
    lp_sub_key: *const u16,
    reserved: u32,
    _lp_class: *const u16,
    dw_options: u32,
    _sam_desired: u32,
    _lp_security_attributes: *const c_void,
    phk_result: *mut HKEY,
    lpdw_disposition: *mut u32,
) -> Lstatus {
    // SAFETY: out pointers written only when non-null.
    unsafe {
        if !phk_result.is_null() {
            *phk_result = ptr::null_mut();
        }
        if !lpdw_disposition.is_null() {
            *lpdw_disposition = 0;
        }
    }
    if reserved != 0 || dw_options != 0 {
        return INVALID_PARAMETER;
    }
    // SAFETY: lp_sub_key is a NUL-terminated wide string or null.
    let sub = unsafe { subkey_path(lp_sub_key) };
    let s = session();
    match s.with_registry(|reg| reg_ops::create_key(reg, s.handles(), h_key as usize, &sub)) {
        Ok(handle) => {
            // SAFETY: phk_result written only when non-null.
            unsafe {
                if !phk_result.is_null() {
                    *phk_result = handle as HKEY;
                }
            }
            SUCCESS
        }
        Err(code) => code,
    }
}

/// `RegCloseKey`: release a minted handle (predefined handles are no-ops).
#[unsafe(no_mangle)]
pub extern "system" fn mRegCloseKey(h_key: HKEY) -> Lstatus {
    reg_ops::close_key(session().handles(), h_key as usize)
}

/// `RegSetValueExW`: write a value on the key named by `h_key`.
#[unsafe(no_mangle)]
pub extern "system" fn mRegSetValueExW(
    h_key: HKEY,
    lp_value_name: *const u16,
    reserved: u32,
    dw_type: u32,
    lp_data: *const u8,
    cb_data: u32,
) -> Lstatus {
    if reserved != 0 {
        return INVALID_PARAMETER;
    }
    // SAFETY: lp_value_name is a NUL-terminated wide string or null.
    let name = unsafe { value_name(lp_value_name) };
    let data = if lp_data.is_null() || cb_data == 0 {
        Vec::new()
    } else {
        // SAFETY: caller guarantees cb_data readable bytes at lp_data.
        unsafe { ptr::slice_from_raw_parts(lp_data, cb_data as usize).as_ref() }
            .map(<[u8]>::to_vec)
            .unwrap_or_default()
    };
    let s = session();
    match s.with_registry(|reg| {
        reg_ops::set_value(reg, s.handles(), h_key as usize, &name, dw_type, &data)
    }) {
        Ok(()) => SUCCESS,
        Err(code) => code,
    }
}

/// `RegQueryValueExW`: read a value from the key named by `h_key`.
#[unsafe(no_mangle)]
pub extern "system" fn mRegQueryValueExW(
    h_key: HKEY,
    lp_value_name: *const u16,
    lp_reserved: *const u32,
    lp_type: *mut u32,
    lp_data: *mut u8,
    lpcb_data: *mut u32,
) -> Lstatus {
    if !lp_reserved.is_null() {
        return INVALID_PARAMETER;
    }
    if !lp_data.is_null() && lpcb_data.is_null() {
        return INVALID_PARAMETER;
    }
    // SAFETY: lp_value_name is a NUL-terminated wide string or null.
    let name = unsafe { value_name(lp_value_name) };
    let s = session();
    let (win32_type, bytes) =
        match s.with_registry(|reg| reg_ops::query_value(reg, s.handles(), h_key as usize, &name)) {
            Ok(value) => value,
            Err(code) => return code,
        };
    // SAFETY: out pointers validated by the caller contract.
    unsafe { fill_value_buffer(&bytes, win32_type, lp_type, lp_data, lpcb_data) }
}

/// `RegDeleteValueW`: delete a value from the key named by `h_key`.
#[unsafe(no_mangle)]
pub extern "system" fn mRegDeleteValueW(h_key: HKEY, lp_value_name: *const u16) -> Lstatus {
    // SAFETY: lp_value_name is a NUL-terminated wide string or null.
    let name = unsafe { value_name(lp_value_name) };
    let s = session();
    match s.with_registry(|reg| reg_ops::delete_value(reg, s.handles(), h_key as usize, &name)) {
        Ok(()) => SUCCESS,
        Err(code) => code,
    }
}

/// `RegGetValueW`: read a value from a subkey of `h_key`.
#[unsafe(no_mangle)]
pub extern "system" fn mRegGetValueW(
    h_key: HKEY,
    lp_sub_key: *const u16,
    lp_value: *const u16,
    _dw_flags: u32,
    pdw_type: *mut u32,
    pv_data: *mut c_void,
    pcb_data: *mut u32,
) -> Lstatus {
    if !pv_data.is_null() && pcb_data.is_null() {
        return INVALID_PARAMETER;
    }
    // SAFETY: both pointers are NUL-terminated wide strings or null.
    let sub = unsafe { subkey_path(lp_sub_key) };
    // SAFETY: see above.
    let name = unsafe { value_name(lp_value) };
    let s = session();
    let (win32_type, bytes) = match s.with_registry(|reg| {
        reg_ops::get_value_under(reg, s.handles(), h_key as usize, &sub, &name)
    }) {
        Ok(value) => value,
        Err(code) => return code,
    };
    // SAFETY: out pointers validated by the caller contract.
    unsafe { fill_value_buffer(&bytes, win32_type, pdw_type, pv_data.cast::<u8>(), pcb_data) }
}

/// `RegEnumKeyExW`: return the subkey name at `dw_index` in ordinal order.
#[unsafe(no_mangle)]
pub extern "system" fn mRegEnumKeyExW(
    h_key: HKEY,
    dw_index: u32,
    lp_name: *mut u16,
    lpcch_name: *mut u32,
    lp_reserved: *const u32,
    _lp_class: *mut u16,
    lpcch_class: *mut u32,
    lpft_last_write_time: *mut FILETIME,
) -> Lstatus {
    if !lp_reserved.is_null() || lpcch_name.is_null() {
        return INVALID_PARAMETER;
    }
    let s = session();
    let name = match s
        .with_registry(|reg| reg_ops::enum_key(reg, s.handles(), h_key as usize, dw_index))
    {
        Ok(Some(name)) => name,
        Ok(None) => return NO_MORE_ITEMS,
        Err(code) => return code,
    };
    // SAFETY: optional out pointers written only when non-null.
    unsafe {
        if !lpcch_class.is_null() {
            *lpcch_class = 0;
        }
        if !lpft_last_write_time.is_null() {
            *lpft_last_write_time = FILETIME {
                dwLowDateTime: 0,
                dwHighDateTime: 0,
            };
        }
        write_wide_name(name.as_units(), lp_name, lpcch_name)
    }
}

/// `RegEnumValueW`: return the value at `dw_index` in ordinal order.
#[unsafe(no_mangle)]
pub extern "system" fn mRegEnumValueW(
    h_key: HKEY,
    dw_index: u32,
    lp_value_name: *mut u16,
    lpcch_value_name: *mut u32,
    lp_reserved: *const u32,
    lp_type: *mut u32,
    lp_data: *mut u8,
    lpcb_data: *mut u32,
) -> Lstatus {
    if !lp_reserved.is_null() || lpcch_value_name.is_null() {
        return INVALID_PARAMETER;
    }
    if !lp_data.is_null() && lpcb_data.is_null() {
        return INVALID_PARAMETER;
    }
    let s = session();
    let (name, win32_type, bytes) = match s
        .with_registry(|reg| reg_ops::enum_value(reg, s.handles(), h_key as usize, dw_index))
    {
        Ok(Some(value)) => value,
        Ok(None) => return NO_MORE_ITEMS,
        Err(code) => return code,
    };
    // SAFETY: name buffer contract on the caller's in/out count.
    let name_status = unsafe { write_wide_name(name.as_units(), lp_value_name, lpcch_value_name) };
    if name_status != SUCCESS {
        return name_status;
    }
    // SAFETY: data out pointers validated by the caller contract.
    unsafe { fill_value_buffer(&bytes, win32_type, lp_type, lp_data, lpcb_data) }
}

/// `RegQueryInfoKeyW`: report subkey/value counts and maxima.
#[unsafe(no_mangle)]
pub extern "system" fn mRegQueryInfoKeyW(
    h_key: HKEY,
    _lp_class: *mut u16,
    lpcch_class: *mut u32,
    lp_reserved: *const u32,
    lpc_sub_keys: *mut u32,
    lpcb_max_sub_key_len: *mut u32,
    lpcb_max_class_len: *mut u32,
    lpc_values: *mut u32,
    lpcb_max_value_name_len: *mut u32,
    lpcb_max_value_len: *mut u32,
    lpcb_security_descriptor: *mut u32,
    lpft_last_write_time: *mut FILETIME,
) -> Lstatus {
    if !lp_reserved.is_null() {
        return INVALID_PARAMETER;
    }
    let s = session();
    let info =
        match s.with_registry(|reg| reg_ops::query_info(reg, s.handles(), h_key as usize)) {
            Ok(info) => info,
            Err(code) => return code,
        };
    // SAFETY: each out pointer written only when non-null.
    unsafe {
        if !lpcch_class.is_null() {
            *lpcch_class = 0;
        }
        if !lpc_sub_keys.is_null() {
            *lpc_sub_keys = info.subkeys;
        }
        if !lpcb_max_sub_key_len.is_null() {
            *lpcb_max_sub_key_len = info.max_subkey_len;
        }
        if !lpcb_max_class_len.is_null() {
            *lpcb_max_class_len = 0;
        }
        if !lpc_values.is_null() {
            *lpc_values = info.values;
        }
        if !lpcb_max_value_name_len.is_null() {
            *lpcb_max_value_name_len = info.max_value_name_len;
        }
        if !lpcb_max_value_len.is_null() {
            *lpcb_max_value_len = info.max_value_len;
        }
        if !lpcb_security_descriptor.is_null() {
            *lpcb_security_descriptor = 0;
        }
        if !lpft_last_write_time.is_null() {
            *lpft_last_write_time = FILETIME {
                dwLowDateTime: 0,
                dwHighDateTime: 0,
            };
        }
    }
    SUCCESS
}

/// `RegDeleteKeyExW`: delete a subkey and its subtree.
#[unsafe(no_mangle)]
pub extern "system" fn mRegDeleteKeyExW(
    h_key: HKEY,
    lp_sub_key: *const u16,
    _sam_desired: u32,
    _reserved: u32,
) -> Lstatus {
    // SAFETY: lp_sub_key is a NUL-terminated wide string or null.
    let sub = unsafe { subkey_path(lp_sub_key) };
    let s = session();
    match s.with_registry(|reg| reg_ops::delete_key(reg, s.handles(), h_key as usize, &sub)) {
        Ok(()) => SUCCESS,
        Err(code) => code,
    }
}

// --- NOT_SUPPORTED W-form stubs (MW2-4) --------------------------------------
//
// These W-form entry points exist for ABI completeness but are out of MW2
// scope. They return `ERROR_NOT_SUPPORTED`; out-handle stubs additionally null
// their `PHKEY` so a caller never observes an uninitialized handle.

/// Generate a stub that simply returns `ERROR_NOT_SUPPORTED`. Parameters are
/// anonymous (their values are ignored).
macro_rules! not_supported_stub {
    ($( $(#[$meta:meta])* $name:ident ( $($pty:ty),* $(,)? ) );+ $(;)?) => {
        $(
            $(#[$meta])*
            #[unsafe(no_mangle)]
            pub extern "system" fn $name( $(_: $pty),* ) -> Lstatus {
                NOT_SUPPORTED
            }
        )+
    };
}

not_supported_stub! {
    /// `RegOverridePredefKey`.
    mRegOverridePredefKey(HKEY, HKEY);
    /// `RegDisablePredefinedCache`.
    mRegDisablePredefinedCache();
    /// `RegDisablePredefinedCacheEx`.
    mRegDisablePredefinedCacheEx();
    /// `RegFlushKey`.
    mRegFlushKey(HKEY);
    /// `RegSaveKeyW`.
    mRegSaveKeyW(HKEY, *const u16, *const c_void);
    /// `RegRestoreKeyW`.
    mRegRestoreKeyW(HKEY, *const u16, u32);
    /// `RegLoadKeyW`.
    mRegLoadKeyW(HKEY, *const u16, *const u16);
    /// `RegUnLoadKeyW`.
    mRegUnLoadKeyW(HKEY, *const u16);
    /// `RegReplaceKeyW`.
    mRegReplaceKeyW(HKEY, *const u16, *const u16, *const u16);
    /// `RegRenameKey`.
    mRegRenameKey(HKEY, *const u16, *const u16);
    /// `RegNotifyChangeKeyValue`.
    mRegNotifyChangeKeyValue(HKEY, i32, u32, *mut c_void, i32);
    /// `RegQueryMultipleValuesW`.
    mRegQueryMultipleValuesW(HKEY, *mut c_void, u32, *mut u16, *mut u32);
    /// `RegSetKeyValueW`.
    mRegSetKeyValueW(HKEY, *const u16, *const u16, u32, *const c_void, u32);
    /// `RegDeleteKeyValueW`.
    mRegDeleteKeyValueW(HKEY, *const u16, *const u16);
    /// `RegDeleteTreeW`.
    mRegDeleteTreeW(HKEY, *const u16);
    /// `RegCopyTreeW`.
    mRegCopyTreeW(HKEY, *const u16, HKEY);
    /// `RegQueryValueW` (legacy default-value form).
    mRegQueryValueW(HKEY, *const u16, *mut u16, *mut i32);
    /// `RegSetValueW` (legacy default-value form).
    mRegSetValueW(HKEY, *const u16, u32, *const u16, u32);
    /// `RegDisableReflectionKey`.
    mRegDisableReflectionKey(HKEY);
    /// `RegEnableReflectionKey`.
    mRegEnableReflectionKey(HKEY);
    /// `RegQueryReflectionKey`.
    mRegQueryReflectionKey(HKEY, *mut i32);
    /// `RegGetKeySecurity`.
    mRegGetKeySecurity(HKEY, u32, *mut c_void, *mut u32);
    /// `RegSetKeySecurity`.
    mRegSetKeySecurity(HKEY, u32, *mut c_void);
    /// `RegDeleteKeyTransactedW`.
    mRegDeleteKeyTransactedW(HKEY, *const u16, u32, u32, *mut c_void, *mut c_void);
}

/// Write a null `HKEY` through a possibly-null `PHKEY` out pointer.
///
/// # Safety
///
/// `phk` must be null or valid for a `HKEY` write.
unsafe fn null_out_handle(phk: *mut HKEY) {
    // SAFETY: written only when non-null.
    unsafe {
        if !phk.is_null() {
            *phk = ptr::null_mut();
        }
    }
}

/// `RegCreateKeyTransactedW` (out-handle stub).
#[unsafe(no_mangle)]
pub extern "system" fn mRegCreateKeyTransactedW(
    _h_key: HKEY,
    _lp_sub_key: *const u16,
    _reserved: u32,
    _lp_class: *const u16,
    _dw_options: u32,
    _sam_desired: u32,
    _lp_security_attributes: *const c_void,
    phk_result: *mut HKEY,
    lpdw_disposition: *mut u32,
    _h_transaction: *mut c_void,
    _p_extended_parameter: *mut c_void,
) -> Lstatus {
    // SAFETY: out pointers written only when non-null.
    unsafe {
        null_out_handle(phk_result);
        if !lpdw_disposition.is_null() {
            *lpdw_disposition = 0;
        }
    }
    NOT_SUPPORTED
}

/// `RegOpenKeyTransactedW` (out-handle stub).
#[unsafe(no_mangle)]
pub extern "system" fn mRegOpenKeyTransactedW(
    _h_key: HKEY,
    _lp_sub_key: *const u16,
    _ul_options: u32,
    _sam_desired: u32,
    phk_result: *mut HKEY,
    _h_transaction: *mut c_void,
    _p_extended_parameter: *mut c_void,
) -> Lstatus {
    // SAFETY: out pointer written only when non-null.
    unsafe { null_out_handle(phk_result) };
    NOT_SUPPORTED
}

/// `RegOpenCurrentUser` (out-handle stub).
#[unsafe(no_mangle)]
pub extern "system" fn mRegOpenCurrentUser(_sam_desired: u32, phk_result: *mut HKEY) -> Lstatus {
    // SAFETY: out pointer written only when non-null.
    unsafe { null_out_handle(phk_result) };
    NOT_SUPPORTED
}

/// `RegOpenUserClassesRoot` (out-handle stub).
#[unsafe(no_mangle)]
pub extern "system" fn mRegOpenUserClassesRoot(
    _h_token: *mut c_void,
    _dw_options: u32,
    _sam_desired: u32,
    phk_result: *mut HKEY,
) -> Lstatus {
    // SAFETY: out pointer written only when non-null.
    unsafe { null_out_handle(phk_result) };
    NOT_SUPPORTED
}

/// `RegConnectRegistryW` (out-handle stub).
#[unsafe(no_mangle)]
pub extern "system" fn mRegConnectRegistryW(
    _lp_machine_name: *const u16,
    _h_key: HKEY,
    phk_result: *mut HKEY,
) -> Lstatus {
    // SAFETY: out pointer written only when non-null.
    unsafe { null_out_handle(phk_result) };
    NOT_SUPPORTED
}

/// `RegConnectRegistryExW` (out-handle stub).
#[unsafe(no_mangle)]
pub extern "system" fn mRegConnectRegistryExW(
    _lp_machine_name: *const u16,
    _h_key: HKEY,
    _flags: u32,
    phk_result: *mut HKEY,
) -> Lstatus {
    // SAFETY: out pointer written only when non-null.
    unsafe { null_out_handle(phk_result) };
    NOT_SUPPORTED
}
