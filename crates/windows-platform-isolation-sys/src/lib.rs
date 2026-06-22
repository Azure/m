// Copyright (c) Microsoft Corporation.

//! Unsafe FFI leaf for the `windows-platform-isolation` registry surface.
//!
//! This is the registry **`unsafe` leaf** (D1 / D13, Option B): it wraps the
//! handle-management-critical Win32 registry entry points as **safe**,
//! slice-in / owned-out functions over a RAII [`RegKey`] handle. No raw
//! pointers and no `HKEY` lifetimes cross this crate's boundary; every
//! two-call length probe, `RegCloseKey`, and `WIN32_ERROR` mapping is confined
//! here.
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
