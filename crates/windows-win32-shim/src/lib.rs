// Copyright (c) Microsoft Corporation.

//! Parallel, all-Rust reimplementation of the C++ `mwin32` DLL.
//!
//! This crate exposes a Win32-shaped C ABI (the `m`-prefixed entry points —
//! `mRegOpenKeyExW`, `mCreateFileW`, …) whose bodies route through the safe
//! `windows-platform-isolation` crate instead of the live OS API directly. It
//! is **not** layered on the C++ implementation (SHIM-D1); it targets the same
//! exported ABI and the same `.pilcfg` / saved-state artifacts.
//!
//! The C ABI inherently takes raw caller pointers, so the crate cannot be
//! `#![forbid(unsafe_code)]`. Instead the crate root is `#![deny(unsafe_code)]`
//! and only the ABI-boundary helpers carry `#[allow(unsafe_code)]` (SHIM-D2);
//! all isolation logic stays in the safe `windows-platform-isolation` crate.
//!
//! This crate is Windows-only; on other platforms it compiles to nothing.
//!
//! The current milestone (MW1) provides the foundation: Win32 error mapping
//! ([`error_map`]), the minted-handle table ([`handle_table`]), and the
//! process-wide [`session`]. The C ABI entry points themselves arrive in later
//! milestones (MW2 registry, MW3 filesystem).

#![deny(unsafe_code)]

#[cfg(windows)]
pub mod error_map;

#[cfg(windows)]
pub mod handle_table;

#[cfg(windows)]
pub mod session;

#[cfg(windows)]
pub use error_map::{filesystem_error_to_win32, registry_error_to_lstatus, set_last_error, Lstatus};

#[cfg(windows)]
pub use handle_table::{
    FileHandleState, FindEnumerationState, HandlePayload, HandleTable, RawHandle, predefined_root,
};

#[cfg(windows)]
pub use session::{ShimSession, session};
