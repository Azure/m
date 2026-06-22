// Copyright (c) Microsoft Corporation.

//! The Win32 filesystem C ABI (`m*W` entry points) — MW3.
//!
//! Each exported symbol mirrors a Win32 `*W` filesystem prototype so the shim is
//! a drop-in for the C++ `mwin32` filesystem surface. A body does only three
//! things: marshal raw caller pointers into owned Rust values, delegate to the
//! safe surface-generic core ([`crate::fs_ops`]) over the process-wide live
//! [`session`], and marshal results back out through the caller's buffers and
//! the Win32 `BOOL` / `HANDLE` / `SetLastError` convention.
//!
//! Per SHIM-D2 this is the only place raw caller pointers are touched, so the
//! module opts back into `unsafe_code` (the crate root denies it). The exported
//! functions take raw pointers and dereference them — that is the C ABI contract
//! (callers are C, never safe Rust), so the two FFI-boundary clippy lints below
//! are allowed module-wide rather than papered over per call:
//! - `not_unsafe_ptr_arg_deref`: every entry point is a C export, not a Rust API;
//! - `too_many_arguments`: the argument lists are fixed by the Win32 prototypes.
//!
//! ## Scope (SHIM-D12)
//!
//! Metadata, directory, and enumeration verbs are implemented. Byte-content and
//! move/copy verbs (`mReadFile`, `mWriteFile`, `mReadFileScatter` /
//! `mWriteFileGather`, `mMoveFileExW`, `mCopyFileExW`) exist for ABI
//! completeness but report the Win32 not-supported shape (`FALSE` +
//! `ERROR_NOT_SUPPORTED`); `mMoveFileExW` additionally awaits a future isolation
//! rename verb.

#![allow(unsafe_code)]
#![allow(clippy::not_unsafe_ptr_arg_deref, clippy::too_many_arguments)]

use core::ffi::c_void;

use windows_platform_isolation::{DirEntry, FilePath, NodeKind};
use windows_sys::Win32::Foundation::{
    CloseHandle, ERROR_FILE_NOT_FOUND, ERROR_INVALID_HANDLE, ERROR_INVALID_PARAMETER,
    ERROR_NO_MORE_FILES, ERROR_NOT_SUPPORTED, FALSE, FILETIME, HANDLE, INVALID_HANDLE_VALUE, TRUE,
};
use windows_sys::Win32::Foundation::BOOL;
use windows_sys::Win32::Storage::FileSystem::{
    GET_FILEEX_INFO_LEVELS, GetFileExInfoStandard, INVALID_FILE_ATTRIBUTES, WIN32_FILE_ATTRIBUTE_DATA,
    WIN32_FIND_DATAW,
};

use crate::error_map::set_last_error;
use crate::fs_ops;
use crate::handle_table::is_minted_value;
use crate::session::session;

/// The shift isolating the high 32 bits of a 64-bit byte size (no manifest
/// literals in logic).
const SIZE_HIGH_SHIFT: u32 = 32;
/// The mask isolating the low 32 bits of a 64-bit byte size.
const SIZE_LOW_MASK: u64 = 0xFFFF_FFFF;
/// The shift isolating the high 32 bits of a 64-bit `FILETIME` tick count.
const FILETIME_HIGH_SHIFT: i64 = 32;

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
        core::ptr::slice_from_raw_parts(p, len)
            .as_ref()
            .map(<[u16]>::to_vec)
            .unwrap_or_default()
    }
}

/// Marshal an `lpFileName` / `lpPathName` pointer into a [`FilePath`].
///
/// # Safety
///
/// `p` must be null or a NUL-terminated wide string.
unsafe fn to_file_path(p: *const u16) -> FilePath {
    // SAFETY: forwarded contract.
    FilePath::from_units(unsafe { read_wide_units(p) })
}

// --- Win32 projection helpers -----------------------------------------------

/// Reinterpret a 64-bit `FILETIME` tick count (as the isolation surface stores
/// timestamps) into the Win32 high / low [`FILETIME`] halves.
fn to_filetime(ticks: i64) -> FILETIME {
    FILETIME {
        dwLowDateTime: ticks as u32,
        dwHighDateTime: (ticks >> FILETIME_HIGH_SHIFT) as u32,
    }
}

/// Fill a [`WIN32_FIND_DATAW`] from a directory entry. The UTF-16 name is copied
/// into the fixed `cFileName` buffer and truncated (with a guaranteed NUL) if it
/// does not fit; the short-name and reserved fields are cleared.
///
/// # Safety
///
/// `out` must point to a writable [`WIN32_FIND_DATAW`].
unsafe fn fill_find_data(entry: &DirEntry, out: *mut WIN32_FIND_DATAW) {
    let size = entry.metadata.size;
    // SAFETY: `out` is a writable WIN32_FIND_DATAW; every field is plain integer
    // data, so any prior bit pattern is a valid value to overwrite.
    let out = unsafe { &mut *out };
    out.dwFileAttributes = fs_ops::to_win32_attributes(&entry.metadata, entry.kind);
    out.ftCreationTime = to_filetime(entry.metadata.creation_time);
    out.ftLastAccessTime = to_filetime(entry.metadata.last_access_time);
    out.ftLastWriteTime = to_filetime(entry.metadata.last_write_time);
    out.nFileSizeHigh = (size >> SIZE_HIGH_SHIFT) as u32;
    out.nFileSizeLow = (size & SIZE_LOW_MASK) as u32;
    out.dwReserved0 = 0;
    out.dwReserved1 = 0;
    out.cAlternateFileName = [0u16; 14];

    out.cFileName = [0u16; 260];
    let units = entry.name.as_units();
    let capacity = out.cFileName.len();
    let n = core::cmp::min(units.len(), capacity - 1);
    out.cFileName[..n].copy_from_slice(&units[..n]);
}

/// Fill a [`WIN32_FILE_ATTRIBUTE_DATA`] (the `GetFileExInfoStandard` payload)
/// from a node's metadata and kind.
///
/// # Safety
///
/// `out` must point to a writable [`WIN32_FILE_ATTRIBUTE_DATA`].
unsafe fn fill_attribute_data(
    metadata: &windows_platform_isolation::FileMetadata,
    kind: NodeKind,
    out: *mut WIN32_FILE_ATTRIBUTE_DATA,
) {
    let size = metadata.size;
    // SAFETY: `out` is a writable WIN32_FILE_ATTRIBUTE_DATA of plain integers.
    let out = unsafe { &mut *out };
    out.dwFileAttributes = fs_ops::to_win32_attributes(metadata, kind);
    out.ftCreationTime = to_filetime(metadata.creation_time);
    out.ftLastAccessTime = to_filetime(metadata.last_access_time);
    out.ftLastWriteTime = to_filetime(metadata.last_write_time);
    out.nFileSizeHigh = (size >> SIZE_HIGH_SHIFT) as u32;
    out.nFileSizeLow = (size & SIZE_LOW_MASK) as u32;
}

// --- Implemented filesystem W entry points (MW3-1..MW3-4) --------------------

/// `CreateFileW`: open or create the file (or backup-semantics directory) named
/// by `lp_file_name`, minting a file `HANDLE`. Returns `INVALID_HANDLE_VALUE` on
/// failure.
#[unsafe(no_mangle)]
pub extern "system" fn mCreateFileW(
    lp_file_name: *const u16,
    _dw_desired_access: u32,
    _dw_share_mode: u32,
    _lp_security_attributes: *const c_void,
    dw_creation_disposition: u32,
    dw_flags_and_attributes: u32,
    _h_template_file: HANDLE,
) -> HANDLE {
    if lp_file_name.is_null() {
        set_last_error(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    // SAFETY: lp_file_name is a NUL-terminated wide string (checked non-null).
    let path = unsafe { to_file_path(lp_file_name) };
    let s = session();
    match s.with_filesystem(|fs| {
        fs_ops::create_file(
            fs,
            s.handles(),
            &path,
            dw_creation_disposition,
            dw_flags_and_attributes,
        )
    }) {
        Ok(handle) => handle as HANDLE,
        Err(code) => {
            set_last_error(code);
            INVALID_HANDLE_VALUE
        }
    }
}

/// `CloseHandle`: release a value the shim minted; forward any genuine OS handle
/// to the real `CloseHandle` (SHIM-D12 — this entry point sees all `CloseHandle`
/// traffic, unlike `mRegCloseKey`).
#[unsafe(no_mangle)]
pub extern "system" fn mCloseHandle(h_object: HANDLE) -> BOOL {
    let raw = h_object as usize;
    if !is_minted_value(raw) {
        // SAFETY: a non-minted value is a genuine OS handle; CloseHandle accepts
        // any handle value and reports its own failure.
        return unsafe { CloseHandle(h_object) };
    }
    if session().handles().close(raw) {
        TRUE
    } else {
        set_last_error(ERROR_INVALID_HANDLE);
        FALSE
    }
}

/// `GetFileAttributesW`: the Win32 attribute bitmask of the node named by
/// `lp_file_name`, or `INVALID_FILE_ATTRIBUTES` on failure.
#[unsafe(no_mangle)]
pub extern "system" fn mGetFileAttributesW(lp_file_name: *const u16) -> u32 {
    if lp_file_name.is_null() {
        set_last_error(ERROR_INVALID_PARAMETER);
        return INVALID_FILE_ATTRIBUTES;
    }
    // SAFETY: lp_file_name is a NUL-terminated wide string (checked non-null).
    let path = unsafe { to_file_path(lp_file_name) };
    let s = session();
    match s.with_filesystem(|fs| fs_ops::stat_path(fs, &path)) {
        Ok((metadata, kind)) => fs_ops::to_win32_attributes(&metadata, kind),
        Err(code) => {
            set_last_error(code);
            INVALID_FILE_ATTRIBUTES
        }
    }
}

/// `GetFileAttributesExW`: fill a [`WIN32_FILE_ATTRIBUTE_DATA`] for the node
/// named by `lp_file_name`. Only `GetFileExInfoStandard` is supported.
#[unsafe(no_mangle)]
pub extern "system" fn mGetFileAttributesExW(
    lp_file_name: *const u16,
    f_info_level_id: GET_FILEEX_INFO_LEVELS,
    lp_file_information: *mut c_void,
) -> BOOL {
    if lp_file_name.is_null()
        || lp_file_information.is_null()
        || f_info_level_id != GetFileExInfoStandard
    {
        set_last_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    // SAFETY: lp_file_name is a NUL-terminated wide string (checked non-null).
    let path = unsafe { to_file_path(lp_file_name) };
    let s = session();
    match s.with_filesystem(|fs| fs_ops::stat_path(fs, &path)) {
        Ok((metadata, kind)) => {
            // SAFETY: lp_file_information is non-null and, per the API contract,
            // points to a WIN32_FILE_ATTRIBUTE_DATA the caller provided.
            unsafe {
                fill_attribute_data(
                    &metadata,
                    kind,
                    lp_file_information as *mut WIN32_FILE_ATTRIBUTE_DATA,
                );
            }
            TRUE
        }
        Err(code) => {
            set_last_error(code);
            FALSE
        }
    }
}

/// `SetFileAttributesW`: accept-and-ignore attribute mutation (SHIM-D12) after
/// validating the node exists.
#[unsafe(no_mangle)]
pub extern "system" fn mSetFileAttributesW(
    lp_file_name: *const u16,
    dw_file_attributes: u32,
) -> BOOL {
    if lp_file_name.is_null() {
        set_last_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    // SAFETY: lp_file_name is a NUL-terminated wide string (checked non-null).
    let path = unsafe { to_file_path(lp_file_name) };
    let s = session();
    match s.with_filesystem(|fs| fs_ops::set_file_attributes(fs, &path, dw_file_attributes)) {
        Ok(()) => TRUE,
        Err(code) => {
            set_last_error(code);
            FALSE
        }
    }
}

/// `DeleteFileW`: remove the file named by `lp_file_name`.
#[unsafe(no_mangle)]
pub extern "system" fn mDeleteFileW(lp_file_name: *const u16) -> BOOL {
    if lp_file_name.is_null() {
        set_last_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    // SAFETY: lp_file_name is a NUL-terminated wide string (checked non-null).
    let path = unsafe { to_file_path(lp_file_name) };
    let s = session();
    match s.with_filesystem(|fs| fs_ops::delete_file(fs, &path)) {
        Ok(()) => TRUE,
        Err(code) => {
            set_last_error(code);
            FALSE
        }
    }
}

/// `CreateDirectoryW`: create the directory named by `lp_path_name`.
#[unsafe(no_mangle)]
pub extern "system" fn mCreateDirectoryW(
    lp_path_name: *const u16,
    _lp_security_attributes: *const c_void,
) -> BOOL {
    if lp_path_name.is_null() {
        set_last_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    // SAFETY: lp_path_name is a NUL-terminated wide string (checked non-null).
    let path = unsafe { to_file_path(lp_path_name) };
    let s = session();
    match s.with_filesystem(|fs| fs_ops::create_directory(fs, &path)) {
        Ok(()) => TRUE,
        Err(code) => {
            set_last_error(code);
            FALSE
        }
    }
}

/// `RemoveDirectoryW`: remove the directory (and its subtree) named by
/// `lp_path_name`.
#[unsafe(no_mangle)]
pub extern "system" fn mRemoveDirectoryW(lp_path_name: *const u16) -> BOOL {
    if lp_path_name.is_null() {
        set_last_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    // SAFETY: lp_path_name is a NUL-terminated wide string (checked non-null).
    let path = unsafe { to_file_path(lp_path_name) };
    let s = session();
    match s.with_filesystem(|fs| fs_ops::remove_directory(fs, &path)) {
        Ok(()) => TRUE,
        Err(code) => {
            set_last_error(code);
            FALSE
        }
    }
}

/// `GetFileSizeEx`: report the byte size of the file behind `h_file` through
/// `lp_file_size` (a `LARGE_INTEGER`).
#[unsafe(no_mangle)]
pub extern "system" fn mGetFileSizeEx(h_file: HANDLE, lp_file_size: *mut i64) -> BOOL {
    if lp_file_size.is_null() {
        set_last_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    let s = session();
    match s.with_filesystem(|fs| fs_ops::get_file_size(fs, s.handles(), h_file as usize)) {
        Ok(size) => {
            // SAFETY: lp_file_size is non-null (checked) and points to a caller
            // LARGE_INTEGER.
            unsafe {
                *lp_file_size = size as i64;
            }
            TRUE
        }
        Err(code) => {
            set_last_error(code);
            FALSE
        }
    }
}

/// `SetFilePointerEx`: move the byte cursor of `h_file`, optionally reporting the
/// new position through `lp_new_file_pointer`.
#[unsafe(no_mangle)]
pub extern "system" fn mSetFilePointerEx(
    h_file: HANDLE,
    li_distance_to_move: i64,
    lp_new_file_pointer: *mut i64,
    dw_move_method: u32,
) -> BOOL {
    let s = session();
    match s.with_filesystem(|fs| {
        fs_ops::set_file_pointer(
            fs,
            s.handles(),
            h_file as usize,
            li_distance_to_move,
            dw_move_method,
        )
    }) {
        Ok(new_position) => {
            if !lp_new_file_pointer.is_null() {
                // SAFETY: lp_new_file_pointer is non-null (checked) and points to
                // a caller LARGE_INTEGER.
                unsafe {
                    *lp_new_file_pointer = new_position as i64;
                }
            }
            TRUE
        }
        Err(code) => {
            set_last_error(code);
            FALSE
        }
    }
}

/// `FindFirstFileW`: begin a directory enumeration, filling the first entry and
/// returning a find `HANDLE` (or `INVALID_HANDLE_VALUE` on failure).
#[unsafe(no_mangle)]
pub extern "system" fn mFindFirstFileW(
    lp_file_name: *const u16,
    lp_find_file_data: *mut WIN32_FIND_DATAW,
) -> HANDLE {
    if lp_file_name.is_null() || lp_find_file_data.is_null() {
        set_last_error(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    // SAFETY: lp_file_name is a NUL-terminated wide string (checked non-null).
    let pattern = unsafe { to_file_path(lp_file_name) };
    let s = session();
    match s.with_filesystem(|fs| fs_ops::find_first(fs, s.handles(), &pattern)) {
        Ok(Some((handle, entry))) => {
            // SAFETY: lp_find_file_data is non-null (checked) and writable.
            unsafe {
                fill_find_data(&entry, lp_find_file_data);
            }
            handle as HANDLE
        }
        Ok(None) => {
            set_last_error(ERROR_FILE_NOT_FOUND);
            INVALID_HANDLE_VALUE
        }
        Err(code) => {
            set_last_error(code);
            INVALID_HANDLE_VALUE
        }
    }
}

/// `FindNextFileW`: fill the next entry of the enumeration behind `h_find_file`,
/// reporting `ERROR_NO_MORE_FILES` (`FALSE`) once exhausted.
#[unsafe(no_mangle)]
pub extern "system" fn mFindNextFileW(
    h_find_file: HANDLE,
    lp_find_file_data: *mut WIN32_FIND_DATAW,
) -> BOOL {
    if lp_find_file_data.is_null() {
        set_last_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    match fs_ops::find_next(session().handles(), h_find_file as usize) {
        Ok(Some(entry)) => {
            // SAFETY: lp_find_file_data is non-null (checked) and writable.
            unsafe {
                fill_find_data(&entry, lp_find_file_data);
            }
            TRUE
        }
        Ok(None) => {
            set_last_error(ERROR_NO_MORE_FILES);
            FALSE
        }
        Err(code) => {
            set_last_error(code);
            FALSE
        }
    }
}

/// `FindClose`: release the find-enumeration handle (always a minted value).
#[unsafe(no_mangle)]
pub extern "system" fn mFindClose(h_find_file: HANDLE) -> BOOL {
    if session().handles().close(h_find_file as usize) {
        TRUE
    } else {
        set_last_error(ERROR_INVALID_HANDLE);
        FALSE
    }
}

// --- NOT_SUPPORTED W-form stubs (MW3-4) --------------------------------------
//
// Byte-content and move/copy verbs exist for ABI completeness but are out of MW3
// scope (SHIM-D12). They report the Win32 not-supported shape: `SetLastError`
// with `ERROR_NOT_SUPPORTED` and a `FALSE` return.

/// Generate a `BOOL` stub that sets `ERROR_NOT_SUPPORTED` and returns `FALSE`.
/// Parameters are anonymous (their values are ignored).
macro_rules! not_supported_bool_stub {
    ($( $(#[$meta:meta])* $name:ident ( $($pty:ty),* $(,)? ) );+ $(;)?) => {
        $(
            $(#[$meta])*
            #[unsafe(no_mangle)]
            pub extern "system" fn $name( $(_: $pty),* ) -> BOOL {
                set_last_error(ERROR_NOT_SUPPORTED);
                FALSE
            }
        )+
    };
}

not_supported_bool_stub! {
    /// `ReadFile`.
    mReadFile(HANDLE, *mut c_void, u32, *mut u32, *mut c_void);
    /// `WriteFile`.
    mWriteFile(HANDLE, *const c_void, u32, *mut u32, *mut c_void);
    /// `ReadFileScatter`.
    mReadFileScatter(HANDLE, *const c_void, u32, *mut u32, *mut c_void);
    /// `WriteFileGather`.
    mWriteFileGather(HANDLE, *const c_void, u32, *mut u32, *mut c_void);
    /// `MoveFileExW` (additionally awaits a future isolation rename verb).
    mMoveFileExW(*const u16, *const u16, u32);
    /// `CopyFileExW`.
    mCopyFileExW(*const u16, *const u16, *mut c_void, *mut c_void, *mut i32, u32);
}
