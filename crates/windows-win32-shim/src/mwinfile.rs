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
    WIN32_ERROR,
};
use windows_sys::Win32::Foundation::BOOL;
use windows_sys::Win32::Storage::FileSystem::{
    FINDEX_INFO_LEVELS, FINDEX_SEARCH_OPS, FIND_FIRST_EX_CASE_SENSITIVE, FIND_FIRST_EX_FLAGS,
    FIND_FIRST_EX_LARGE_FETCH, FIND_FIRST_EX_ON_DISK_ENTRIES_ONLY, FindExInfoBasic,
    FindExInfoStandard, FindExSearchLimitToDirectories, FindExSearchNameMatch,
    GET_FILEEX_INFO_LEVELS, GetFileExInfoStandard, INVALID_FILE_ATTRIBUTES,
    WIN32_FILE_ATTRIBUTE_DATA, WIN32_FIND_DATAA, WIN32_FIND_DATAW,
};

use crate::ansi;
use crate::error_map::set_last_error;
use crate::fs_ops;
use crate::handle_table::is_minted_value;
use crate::handle_table::SearchOp;
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

/// Read a NUL-terminated CP_ACP (`*A`) byte string into owned bytes (excluding
/// the NUL). A null pointer yields an empty vector.
///
/// # Safety
///
/// `p` must be null or point to a NUL-terminated sequence of bytes.
unsafe fn read_ansi_bytes(p: *const u8) -> Vec<u8> {
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
        core::slice::from_raw_parts(p, len).to_vec()
    }
}

/// Marshal an `lpFileName` / `lpPathName` `*A` pointer into a [`FilePath`],
/// decoding the bytes through `CP_ACP` (SHIM-D15).
///
/// # Safety
///
/// `p` must be null or a NUL-terminated CP_ACP byte string.
unsafe fn to_ansi_file_path(p: *const u8) -> FilePath {
    // SAFETY: forwarded contract.
    let bytes = unsafe { read_ansi_bytes(p) };
    FilePath::from_units(ansi::ansi_to_utf16(&bytes).as_units().to_vec())
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
/// does not fit. When `emit_short_name` is set (the `FindExInfoStandard` /
/// plain-`mFindFirstFileW` behavior), the entry's 8.3 short name (sourced from
/// the isolation surface, M10) is copied into `cAlternateFileName` (also
/// truncated with a NUL); when clear (`FindExInfoBasic`) the short name is
/// suppressed and `cAlternateFileName` is left empty. The reserved fields are
/// cleared.
///
/// # Safety
///
/// `out` must point to a writable [`WIN32_FIND_DATAW`].
unsafe fn fill_find_data(entry: &DirEntry, emit_short_name: bool, out: *mut WIN32_FIND_DATAW) {
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
    if emit_short_name
        && let Some(short) = entry.short_name.as_ref()
    {
        let units = short.as_units();
        let capacity = out.cAlternateFileName.len();
        let n = core::cmp::min(units.len(), capacity - 1);
        out.cAlternateFileName[..n].copy_from_slice(&units[..n]);
    }

    out.cFileName = [0u16; 260];
    let units = entry.name.as_units();
    let capacity = out.cFileName.len();
    let n = core::cmp::min(units.len(), capacity - 1);
    out.cFileName[..n].copy_from_slice(&units[..n]);
}

/// Fill a [`WIN32_FIND_DATAA`] from a directory entry, mirroring
/// [`fill_find_data`] but transcoding the names to `CP_ACP` (SHIM-D15). The
/// fixed `cFileName` / `cAlternateFileName` buffers are `[i8; N]`; their bytes
/// are reinterpreted as `u8` (identical layout) so the shared
/// [`ansi::fill_ansi_fixed`] can encode, truncate, NUL-terminate, and zero-fill
/// them.
///
/// # Safety
///
/// `out` must point to a writable [`WIN32_FIND_DATAA`].
unsafe fn fill_find_data_ansi(entry: &DirEntry, emit_short_name: bool, out: *mut WIN32_FIND_DATAA) {
    let size = entry.metadata.size;
    // SAFETY: `out` is a writable WIN32_FIND_DATAA; every field is plain integer
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

    // The 8.3 short name is emitted (transcoded) only when requested; otherwise
    // an empty unit slice zero-fills the buffer.
    let alt_units: &[u16] = if emit_short_name {
        entry.short_name.as_ref().map_or(&[], |s| s.as_units())
    } else {
        &[]
    };
    // SAFETY: cAlternateFileName is a fixed [i8; N] buffer; i8 and u8 share
    // layout, so the byte view is sound for the full length.
    let alt = unsafe {
        core::slice::from_raw_parts_mut(
            out.cAlternateFileName.as_mut_ptr().cast::<u8>(),
            out.cAlternateFileName.len(),
        )
    };
    ansi::fill_ansi_fixed(alt_units, alt);

    // SAFETY: cFileName is a fixed [i8; N] buffer; same i8/u8 byte view.
    let name = unsafe {
        core::slice::from_raw_parts_mut(out.cFileName.as_mut_ptr().cast::<u8>(), out.cFileName.len())
    };
    ansi::fill_ansi_fixed(entry.name.as_units(), name);
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
    // The plain (non-Ex) form enumerates with a case-insensitive name match and
    // emits the 8.3 short name (the `FindExInfoStandard` behavior).
    match s.with_filesystem(|fs| {
        fs_ops::find_first(fs, s.handles(), &pattern, SearchOp::NameMatch, false, true)
    }) {
        Ok(Some((handle, entry))) => {
            // SAFETY: lp_find_file_data is non-null (checked) and writable.
            unsafe {
                fill_find_data(&entry, true, lp_find_file_data);
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

/// Validate the extended `FindFirstFileEx` parameters and map them to the search
/// inputs (`fs_ops::find_first`) plus the 8.3 short-name emission decision.
///
/// Behavior is owned (Design Autonomy): we **specify** which info levels, search
/// operations, and flag bits the shim accepts; `windows-sys`'s named constants
/// are matched (never bare integers).
///
/// - `fInfoLevelId`: `FindExInfoStandard` emits the short name,
///   `FindExInfoBasic` suppresses it; anything else (e.g.
///   `FindExInfoMaxInfoLevel`) is `ERROR_INVALID_PARAMETER`.
/// - `fSearchOp`: `FindExSearchNameMatch` → [`SearchOp::NameMatch`],
///   `FindExSearchLimitToDirectories` → [`SearchOp::LimitToDirectories`];
///   `FindExSearchLimitToDevices` / `FindExSearchMaxSearchOp` are
///   `ERROR_INVALID_PARAMETER`.
/// - `dwAdditionalFlags`: `FIND_FIRST_EX_CASE_SENSITIVE` is honored;
///   `FIND_FIRST_EX_LARGE_FETCH` and `FIND_FIRST_EX_ON_DISK_ENTRIES_ONLY` are
///   accepted and ignored; any other bit is `ERROR_INVALID_PARAMETER`.
/// - `lpSearchFilter` is reserved by Win32 and must be null.
///
/// Returns `(op, case_sensitive, emit_short_name)` on success.
fn map_find_ex_params(
    f_info_level_id: FINDEX_INFO_LEVELS,
    f_search_op: FINDEX_SEARCH_OPS,
    lp_search_filter: *const c_void,
    dw_additional_flags: FIND_FIRST_EX_FLAGS,
) -> Result<(SearchOp, bool, bool), WIN32_ERROR> {
    /// The `dwAdditionalFlags` bits the shim recognizes (accepted; the unlisted
    /// two are merely ignored). Adding or removing a bit changes the accepted
    /// surface, a breaking change.
    const KNOWN_FLAGS: FIND_FIRST_EX_FLAGS = FIND_FIRST_EX_CASE_SENSITIVE
        | FIND_FIRST_EX_LARGE_FETCH
        | FIND_FIRST_EX_ON_DISK_ENTRIES_ONLY;

    if !lp_search_filter.is_null() {
        return Err(ERROR_INVALID_PARAMETER);
    }
    // The windows-sys FINDEX_* constants are camelCase values (not Rust enum
    // variants), so they are compared with `==` rather than matched as patterns
    // (pattern position would trip `non_upper_case_globals`).
    let emit_short_name = if f_info_level_id == FindExInfoStandard {
        true
    } else if f_info_level_id == FindExInfoBasic {
        false
    } else {
        return Err(ERROR_INVALID_PARAMETER);
    };
    let op = if f_search_op == FindExSearchNameMatch {
        SearchOp::NameMatch
    } else if f_search_op == FindExSearchLimitToDirectories {
        SearchOp::LimitToDirectories
    } else {
        return Err(ERROR_INVALID_PARAMETER);
    };
    if dw_additional_flags & !KNOWN_FLAGS != 0 {
        return Err(ERROR_INVALID_PARAMETER);
    }
    let case_sensitive = dw_additional_flags & FIND_FIRST_EX_CASE_SENSITIVE != 0;
    Ok((op, case_sensitive, emit_short_name))
}

/// `FindFirstFileExW`: begin a directory enumeration honoring the extended
/// info-level, search-operation, and flag parameters (SHIM-D14). Returns a find
/// `HANDLE` (or `INVALID_HANDLE_VALUE` on failure).
#[unsafe(no_mangle)]
pub extern "system" fn mFindFirstFileExW(
    lp_file_name: *const u16,
    f_info_level_id: FINDEX_INFO_LEVELS,
    lp_find_file_data: *mut c_void,
    f_search_op: FINDEX_SEARCH_OPS,
    lp_search_filter: *const c_void,
    dw_additional_flags: FIND_FIRST_EX_FLAGS,
) -> HANDLE {
    if lp_file_name.is_null() || lp_find_file_data.is_null() {
        set_last_error(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    let (op, case_sensitive, emit_short_name) = match map_find_ex_params(
        f_info_level_id,
        f_search_op,
        lp_search_filter,
        dw_additional_flags,
    ) {
        Ok(parts) => parts,
        Err(code) => {
            set_last_error(code);
            return INVALID_HANDLE_VALUE;
        }
    };
    let out = lp_find_file_data.cast::<WIN32_FIND_DATAW>();
    // SAFETY: lp_file_name is a NUL-terminated wide string (checked non-null).
    let pattern = unsafe { to_file_path(lp_file_name) };
    let s = session();
    match s.with_filesystem(|fs| {
        fs_ops::find_first(fs, s.handles(), &pattern, op, case_sensitive, emit_short_name)
    }) {
        Ok(Some((handle, entry))) => {
            // SAFETY: out is non-null (checked) and points to a writable
            // WIN32_FIND_DATAW (the documented lpFindFileData contract).
            unsafe {
                fill_find_data(&entry, emit_short_name, out);
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

/// `FindFirstFileTransactedW`: identical to [`mFindFirstFileExW`] but takes a
/// trailing transaction handle. The shim has no transaction surface, so the
/// handle is ignored and the call forwards to the non-transacted path — matching
/// the C++ forwarding stub.
#[unsafe(no_mangle)]
pub extern "system" fn mFindFirstFileTransactedW(
    lp_file_name: *const u16,
    f_info_level_id: FINDEX_INFO_LEVELS,
    lp_find_file_data: *mut c_void,
    f_search_op: FINDEX_SEARCH_OPS,
    lp_search_filter: *const c_void,
    dw_additional_flags: FIND_FIRST_EX_FLAGS,
    _h_transaction: HANDLE,
) -> HANDLE {
    mFindFirstFileExW(
        lp_file_name,
        f_info_level_id,
        lp_find_file_data,
        f_search_op,
        lp_search_filter,
        dw_additional_flags,
    )
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
        Ok(Some((entry, emit_short_name))) => {
            // SAFETY: lp_find_file_data is non-null (checked) and writable.
            unsafe {
                fill_find_data(&entry, emit_short_name, lp_find_file_data);
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

// --- Implemented filesystem A entry points (MW6-3) ---------------------------
//
// Each `*A` form transcodes its path argument from `CP_ACP` to UTF-16
// (SHIM-D15) and then drives the same `fs_ops` cores as its `*W` sibling, so the
// two share one observable behavior. Output names are transcoded back to
// `CP_ACP` in the `WIN32_FIND_DATAA` buffers.

/// `CreateFileA`: the `CP_ACP` form of [`mCreateFileW`].
#[unsafe(no_mangle)]
pub extern "system" fn mCreateFileA(
    lp_file_name: *const u8,
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
    // SAFETY: lp_file_name is a NUL-terminated CP_ACP string (checked non-null).
    let path = unsafe { to_ansi_file_path(lp_file_name) };
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

/// `GetFileAttributesA`: the `CP_ACP` form of [`mGetFileAttributesW`].
#[unsafe(no_mangle)]
pub extern "system" fn mGetFileAttributesA(lp_file_name: *const u8) -> u32 {
    if lp_file_name.is_null() {
        set_last_error(ERROR_INVALID_PARAMETER);
        return INVALID_FILE_ATTRIBUTES;
    }
    // SAFETY: lp_file_name is a NUL-terminated CP_ACP string (checked non-null).
    let path = unsafe { to_ansi_file_path(lp_file_name) };
    let s = session();
    match s.with_filesystem(|fs| fs_ops::stat_path(fs, &path)) {
        Ok((metadata, kind)) => fs_ops::to_win32_attributes(&metadata, kind),
        Err(code) => {
            set_last_error(code);
            INVALID_FILE_ATTRIBUTES
        }
    }
}

/// `GetFileAttributesExA`: the `CP_ACP` form of [`mGetFileAttributesExW`]. The
/// `WIN32_FILE_ATTRIBUTE_DATA` payload holds no strings, so only the path is
/// transcoded.
#[unsafe(no_mangle)]
pub extern "system" fn mGetFileAttributesExA(
    lp_file_name: *const u8,
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
    // SAFETY: lp_file_name is a NUL-terminated CP_ACP string (checked non-null).
    let path = unsafe { to_ansi_file_path(lp_file_name) };
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

/// `SetFileAttributesA`: the `CP_ACP` form of [`mSetFileAttributesW`].
#[unsafe(no_mangle)]
pub extern "system" fn mSetFileAttributesA(
    lp_file_name: *const u8,
    dw_file_attributes: u32,
) -> BOOL {
    if lp_file_name.is_null() {
        set_last_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    // SAFETY: lp_file_name is a NUL-terminated CP_ACP string (checked non-null).
    let path = unsafe { to_ansi_file_path(lp_file_name) };
    let s = session();
    match s.with_filesystem(|fs| fs_ops::set_file_attributes(fs, &path, dw_file_attributes)) {
        Ok(()) => TRUE,
        Err(code) => {
            set_last_error(code);
            FALSE
        }
    }
}

/// `DeleteFileA`: the `CP_ACP` form of [`mDeleteFileW`].
#[unsafe(no_mangle)]
pub extern "system" fn mDeleteFileA(lp_file_name: *const u8) -> BOOL {
    if lp_file_name.is_null() {
        set_last_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    // SAFETY: lp_file_name is a NUL-terminated CP_ACP string (checked non-null).
    let path = unsafe { to_ansi_file_path(lp_file_name) };
    let s = session();
    match s.with_filesystem(|fs| fs_ops::delete_file(fs, &path)) {
        Ok(()) => TRUE,
        Err(code) => {
            set_last_error(code);
            FALSE
        }
    }
}

/// `CreateDirectoryA`: the `CP_ACP` form of [`mCreateDirectoryW`].
#[unsafe(no_mangle)]
pub extern "system" fn mCreateDirectoryA(
    lp_path_name: *const u8,
    _lp_security_attributes: *const c_void,
) -> BOOL {
    if lp_path_name.is_null() {
        set_last_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    // SAFETY: lp_path_name is a NUL-terminated CP_ACP string (checked non-null).
    let path = unsafe { to_ansi_file_path(lp_path_name) };
    let s = session();
    match s.with_filesystem(|fs| fs_ops::create_directory(fs, &path)) {
        Ok(()) => TRUE,
        Err(code) => {
            set_last_error(code);
            FALSE
        }
    }
}

/// `RemoveDirectoryA`: the `CP_ACP` form of [`mRemoveDirectoryW`].
#[unsafe(no_mangle)]
pub extern "system" fn mRemoveDirectoryA(lp_path_name: *const u8) -> BOOL {
    if lp_path_name.is_null() {
        set_last_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    // SAFETY: lp_path_name is a NUL-terminated CP_ACP string (checked non-null).
    let path = unsafe { to_ansi_file_path(lp_path_name) };
    let s = session();
    match s.with_filesystem(|fs| fs_ops::remove_directory(fs, &path)) {
        Ok(()) => TRUE,
        Err(code) => {
            set_last_error(code);
            FALSE
        }
    }
}

/// `FindFirstFileA`: the `CP_ACP` form of [`mFindFirstFileW`].
#[unsafe(no_mangle)]
pub extern "system" fn mFindFirstFileA(
    lp_file_name: *const u8,
    lp_find_file_data: *mut WIN32_FIND_DATAA,
) -> HANDLE {
    if lp_file_name.is_null() || lp_find_file_data.is_null() {
        set_last_error(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    // SAFETY: lp_file_name is a NUL-terminated CP_ACP string (checked non-null).
    let pattern = unsafe { to_ansi_file_path(lp_file_name) };
    let s = session();
    match s.with_filesystem(|fs| {
        fs_ops::find_first(fs, s.handles(), &pattern, SearchOp::NameMatch, false, true)
    }) {
        Ok(Some((handle, entry))) => {
            // SAFETY: lp_find_file_data is non-null (checked) and writable.
            unsafe {
                fill_find_data_ansi(&entry, true, lp_find_file_data);
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

/// `FindFirstFileExA`: the `CP_ACP` form of [`mFindFirstFileExW`].
#[unsafe(no_mangle)]
pub extern "system" fn mFindFirstFileExA(
    lp_file_name: *const u8,
    f_info_level_id: FINDEX_INFO_LEVELS,
    lp_find_file_data: *mut c_void,
    f_search_op: FINDEX_SEARCH_OPS,
    lp_search_filter: *const c_void,
    dw_additional_flags: FIND_FIRST_EX_FLAGS,
) -> HANDLE {
    if lp_file_name.is_null() || lp_find_file_data.is_null() {
        set_last_error(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    let (op, case_sensitive, emit_short_name) = match map_find_ex_params(
        f_info_level_id,
        f_search_op,
        lp_search_filter,
        dw_additional_flags,
    ) {
        Ok(parts) => parts,
        Err(code) => {
            set_last_error(code);
            return INVALID_HANDLE_VALUE;
        }
    };
    let out = lp_find_file_data.cast::<WIN32_FIND_DATAA>();
    // SAFETY: lp_file_name is a NUL-terminated CP_ACP string (checked non-null).
    let pattern = unsafe { to_ansi_file_path(lp_file_name) };
    let s = session();
    match s.with_filesystem(|fs| {
        fs_ops::find_first(fs, s.handles(), &pattern, op, case_sensitive, emit_short_name)
    }) {
        Ok(Some((handle, entry))) => {
            // SAFETY: out is non-null (checked) and points to a writable
            // WIN32_FIND_DATAA (the documented lpFindFileData contract).
            unsafe {
                fill_find_data_ansi(&entry, emit_short_name, out);
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

/// `FindFirstFileTransactedA`: the `CP_ACP` form of
/// [`mFindFirstFileTransactedW`]; the ignored transaction handle forwards to the
/// non-transacted A path.
#[unsafe(no_mangle)]
pub extern "system" fn mFindFirstFileTransactedA(
    lp_file_name: *const u8,
    f_info_level_id: FINDEX_INFO_LEVELS,
    lp_find_file_data: *mut c_void,
    f_search_op: FINDEX_SEARCH_OPS,
    lp_search_filter: *const c_void,
    dw_additional_flags: FIND_FIRST_EX_FLAGS,
    _h_transaction: HANDLE,
) -> HANDLE {
    mFindFirstFileExA(
        lp_file_name,
        f_info_level_id,
        lp_find_file_data,
        f_search_op,
        lp_search_filter,
        dw_additional_flags,
    )
}

/// `FindNextFileA`: the `CP_ACP` form of [`mFindNextFileW`].
#[unsafe(no_mangle)]
pub extern "system" fn mFindNextFileA(
    h_find_file: HANDLE,
    lp_find_file_data: *mut WIN32_FIND_DATAA,
) -> BOOL {
    if lp_find_file_data.is_null() {
        set_last_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    match fs_ops::find_next(session().handles(), h_find_file as usize) {
        Ok(Some((entry, emit_short_name))) => {
            // SAFETY: lp_find_file_data is non-null (checked) and writable.
            unsafe {
                fill_find_data_ansi(&entry, emit_short_name, lp_find_file_data);
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

#[cfg(test)]
mod tests {
    use super::*;
    use windows_platform_isolation::{FileMetadata, Utf16};
    use windows_sys::Win32::Storage::FileSystem::{
        FindExInfoMaxInfoLevel, FindExSearchLimitToDevices, FindExSearchMaxSearchOp,
    };

    fn entry(name: &str, short: Option<&str>, kind: NodeKind) -> DirEntry {
        DirEntry {
            name: Utf16::from_utf8(name),
            kind,
            metadata: FileMetadata::default(),
            short_name: short.map(Utf16::from_utf8),
        }
    }

    /// Decode a fixed `WIN32_FIND_DATAW` name buffer up to its first NUL.
    fn read_wide(buf: &[u16]) -> String {
        let n = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
        String::from_utf16_lossy(&buf[..n])
    }

    fn zeroed_find_data() -> WIN32_FIND_DATAW {
        // SAFETY: WIN32_FIND_DATAW is plain integer/array data; the all-zero bit
        // pattern is a valid (empty) value.
        unsafe { core::mem::zeroed() }
    }

    // --- map_find_ex_params: accepted parameter combinations -----------------

    #[test]
    fn map_find_ex_params_accepts_standard_name_match() {
        let r = map_find_ex_params(FindExInfoStandard, FindExSearchNameMatch, core::ptr::null(), 0);
        assert_eq!(r, Ok((SearchOp::NameMatch, false, true)));
    }

    #[test]
    fn map_find_ex_params_basic_suppresses_short_name() {
        let r = map_find_ex_params(FindExInfoBasic, FindExSearchNameMatch, core::ptr::null(), 0);
        assert_eq!(r, Ok((SearchOp::NameMatch, false, false)));
    }

    #[test]
    fn map_find_ex_params_maps_limit_to_directories() {
        let r = map_find_ex_params(
            FindExInfoStandard,
            FindExSearchLimitToDirectories,
            core::ptr::null(),
            0,
        );
        assert_eq!(r, Ok((SearchOp::LimitToDirectories, false, true)));
    }

    #[test]
    fn map_find_ex_params_honors_case_sensitive_flag() {
        let r = map_find_ex_params(
            FindExInfoStandard,
            FindExSearchNameMatch,
            core::ptr::null(),
            FIND_FIRST_EX_CASE_SENSITIVE,
        );
        assert_eq!(r, Ok((SearchOp::NameMatch, true, true)));
    }

    #[test]
    fn map_find_ex_params_accepts_and_ignores_perf_flags() {
        let flags = FIND_FIRST_EX_LARGE_FETCH | FIND_FIRST_EX_ON_DISK_ENTRIES_ONLY;
        let r = map_find_ex_params(FindExInfoStandard, FindExSearchNameMatch, core::ptr::null(), flags);
        assert_eq!(r, Ok((SearchOp::NameMatch, false, true)));
    }

    // --- map_find_ex_params: rejected parameters -----------------------------

    #[test]
    fn map_find_ex_params_rejects_non_null_filter() {
        let filter = core::ptr::dangling::<c_void>();
        let r = map_find_ex_params(FindExInfoStandard, FindExSearchNameMatch, filter, 0);
        assert_eq!(r, Err(ERROR_INVALID_PARAMETER));
    }

    #[test]
    fn map_find_ex_params_rejects_unsupported_info_level() {
        let r = map_find_ex_params(
            FindExInfoMaxInfoLevel,
            FindExSearchNameMatch,
            core::ptr::null(),
            0,
        );
        assert_eq!(r, Err(ERROR_INVALID_PARAMETER));
    }

    #[test]
    fn map_find_ex_params_rejects_unsupported_search_ops() {
        for op in [FindExSearchLimitToDevices, FindExSearchMaxSearchOp] {
            let r = map_find_ex_params(FindExInfoStandard, op, core::ptr::null(), 0);
            assert_eq!(r, Err(ERROR_INVALID_PARAMETER), "search op {op} must be rejected");
        }
    }

    #[test]
    fn map_find_ex_params_rejects_unknown_flag_bit() {
        let unknown = 0x8000_0000u32;
        let r = map_find_ex_params(FindExInfoStandard, FindExSearchNameMatch, core::ptr::null(), unknown);
        assert_eq!(r, Err(ERROR_INVALID_PARAMETER));
    }

    // --- fill_find_data: 8.3 short-name emission / suppression ----------------

    #[test]
    fn fill_find_data_emits_short_name_for_standard() {
        let e = entry("longfilename.dat", Some("LONGFI~1.DAT"), NodeKind::File);
        let mut data = zeroed_find_data();
        // SAFETY: data is a writable WIN32_FIND_DATAW.
        unsafe { fill_find_data(&e, true, &mut data) };
        assert_eq!(read_wide(&data.cFileName), "longfilename.dat");
        assert_eq!(read_wide(&data.cAlternateFileName), "LONGFI~1.DAT");
    }

    #[test]
    fn fill_find_data_suppresses_short_name_for_basic() {
        let e = entry("longfilename.dat", Some("LONGFI~1.DAT"), NodeKind::File);
        let mut data = zeroed_find_data();
        // SAFETY: data is a writable WIN32_FIND_DATAW.
        unsafe { fill_find_data(&e, false, &mut data) };
        assert_eq!(read_wide(&data.cFileName), "longfilename.dat");
        assert_eq!(read_wide(&data.cAlternateFileName), "");
    }

    #[test]
    fn fill_find_data_leaves_alternate_empty_without_short_name() {
        let e = entry("plain.txt", None, NodeKind::File);
        let mut data = zeroed_find_data();
        // SAFETY: data is a writable WIN32_FIND_DATAW.
        unsafe { fill_find_data(&e, true, &mut data) };
        assert_eq!(read_wide(&data.cFileName), "plain.txt");
        assert_eq!(read_wide(&data.cAlternateFileName), "");
    }
}
