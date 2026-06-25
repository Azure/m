// Copyright (c) Microsoft Corporation.

//! Unsafe FFI leaf for the `windows-platform-isolation` registry and
//! filesystem surfaces.
//!
//! This is the **`unsafe` leaf** (D1 / D13, Option B): it wraps the
//! handle-management-critical Win32 entry points as **safe**, slice-in /
//! owned-out functions over RAII handles. No raw pointers and no `HKEY`
//! lifetimes cross this crate's boundary; every two-call length probe,
//! `RegCloseKey`, and `WIN32_ERROR` mapping is confined here. The registry side
//! exposes [`RegKey`]; the filesystem side ([`FileHandle`] + path-based
//! primitives) underpins the live filesystem provider (D20). Unlike [`RegKey`],
//! [`FileHandle`] deliberately **exposes** its raw OS handle
//! (`std::os::windows::io::AsRawHandle`) so a higher layer can drive overlapped
//! I/O for future async stream support without re-opening the file.
//!
//! The safe `windows-platform-isolation` crate consumes these primitives to
//! build the live ("direct") registry provider (D20) while itself remaining
//! unconditionally `#![forbid(unsafe_code)]`. All higher-level types
//! (`KeyPath`, `ValueData`, the surface seam, error mapping) live there.
//!
//! On non-Windows targets this crate compiles to an empty library, mirroring
//! `windows-text-sys`.

#[cfg(windows)]
mod win32;

#[cfg(windows)]
pub use win32::{RawValue, RegError, RegKey};

#[cfg(windows)]
mod file;

#[cfg(windows)]
pub use file::{
    FileAccess, FileHandle, FileInfo, FindEntry, FsError, create_directory, delete_file,
    file_attributes, read_directory, remove_directory, set_file_attributes,
};

#[cfg(windows)]
mod http;

#[cfg(windows)]
pub use http::{HttpError, HttpReply, http_send};
