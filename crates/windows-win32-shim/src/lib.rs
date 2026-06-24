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
//! The MW2 milestone added the registry C ABI (W forms): the byte ↔
//! [`ValueData`](windows_platform_isolation::ValueData) codec ([`value_codec`]),
//! the safe surface-generic registry core ([`reg_ops`]), and the exported
//! `mReg*W` entry points ([`mwinreg`]). The current milestone (MW3) adds the
//! filesystem C ABI (W forms): the safe surface-generic filesystem core
//! ([`fs_ops`]) and the exported `m*W` entry points ([`mwinfile`]) for metadata,
//! directory, and enumeration verbs (byte content + move/copy are deferred,
//! SHIM-D12). The current milestone (MW4) adds the `.pilcfg` JSON sidecar
//! ([`pilcfg`]) that selects how the [`session`] composes its isolation stack
//! (buffered / persisted-state / live passthrough; SHIM-D13). All build on the
//! MW1 foundation: Win32 error mapping ([`error_map`]), the minted-handle table
//! ([`handle_table`]), and the process-wide [`session`].

#![deny(unsafe_code)]

// Platform-independent (no `unsafe`, no Windows deps): the Win32→`m` link-time
// alias generator and its embedded export manifest (MW5, SHIM-D4).
pub mod alias_gen;

// Platform-independent (no `unsafe`, no Windows deps): the NDJSON-driven Win32→`m`
// alias COFF-object emitter — writes the alias artifact with no C++ compiler and
// no MSVC tool involved (MW5, SHIM-D4).
pub mod alias_obj;

#[cfg(windows)]
pub mod ansi;

#[cfg(windows)]
pub mod error_map;

#[cfg(windows)]
pub mod fs_ops;

#[cfg(windows)]
pub mod handle_table;

#[cfg(windows)]
pub mod loader;

#[cfg(windows)]
pub mod mwinfile;

#[cfg(windows)]
pub mod mwinload;

#[cfg(windows)]
pub mod mwinreg;

#[cfg(windows)]
pub mod pilcfg;

#[cfg(windows)]
pub mod reg_ops;

#[cfg(windows)]
pub mod session;

#[cfg(windows)]
pub mod value_codec;

#[cfg(windows)]
pub use error_map::{filesystem_error_to_win32, registry_error_to_lstatus, set_last_error, Lstatus};

#[cfg(windows)]
pub use handle_table::{
    FileHandleState, FindEnumerationState, HandlePayload, HandleTable, RawHandle, SearchOp,
    SearchPredicate, predefined_root,
};

#[cfg(windows)]
pub use loader::{
    EngineSubstitution, LoaderEvent, LoaderMode, LoaderState, ModuleEntry, ModuleTable, NullSink,
    ObservationSink, ProcQuery, RawModule, ShimProc, ShimProcTable,
};

#[cfg(windows)]
pub use pilcfg::{Pilcfg, PilcfgError, expand_environment_path, load_pilcfg, parse_pilcfg};

#[cfg(windows)]
pub use reg_ops::{KeyInfo, QueryBuffer, apply_query_buffer};

#[cfg(windows)]
pub use session::{ShimSession, session};
