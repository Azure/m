//! Windows platform isolation — registry surface (a Rust reimplementation of the
//! C++ Platform Isolation Library, PIL).
//!
//! This crate is the **safe half** (D13): all stateful logic — the overlay /
//! copy-on-write tree, the reified operation seam, the decorator stack, and the
//! typed facade — is memory-safe Rust under `#![forbid(unsafe_code)]`. Per
//! Option B (D13), every `unsafe` Win32 call lives in a separate `-sys` leaf
//! crate (the ordinal-casing / transcoding FFI is in `windows-text-sys` behind
//! the safe [`windows-text`](windows_text) crate); this crate carries no
//! `unsafe` at all.
//!
//! Ingress initially came from loading saved C++ provider state (the shared
//! artifact format, D5) rather than a live provider (D15). M5 adds the live
//! ("direct") Win32 registry provider (D20) — [`LiveRegistry`], Windows-only,
//! whose `unsafe` lives in the `windows-platform-isolation-sys` leaf so this
//! crate still carries none.
//!
//! See `DESIGN-NOTES.md` for the governing decisions (D1–D20).

#![forbid(unsafe_code)]

pub mod decorator;
pub mod error;
pub mod file_path;
pub mod fs;
pub mod fs_error;
pub mod fs_serial;
pub mod fs_surface;
pub mod fs_tree;
#[cfg(windows)]
pub mod live;
#[cfg(windows)]
pub mod live_fs;
pub mod path;
pub mod registry;
pub mod serial;
pub mod surface;
pub mod tree;
pub mod web;

pub use decorator::{Buffered, PassThrough};
pub use error::{RegistryError, Result};
pub use file_path::{FilePath, FileRoot, FileRootKind, PathSurface};
pub use fs::{Filesystem, FsSession};
pub use fs_error::{FilesystemError, FilesystemResult};
pub use fs_serial::load_filesystem;
pub use fs_surface::{FsPassThrough, FsRequest, FsResponse, FsSurface, TreeFsSurface};
pub use fs_tree::{DirEntry, FileMetadata, FileTree, NodeKind, OverlayFileTree};
#[cfg(windows)]
pub use live::LiveRegistry;
#[cfg(windows)]
pub use live_fs::LiveFilesystem;
pub use path::KeyPath;
pub use registry::{Registry, Session, WellKnownRoot};
pub use serial::{load_registry_hive, save_registry_hive};
pub use surface::{Request, Response, Surface, TreeSurface};
pub use tree::{Hive, OverlayTree, ValueData, ValueType};
pub use web::{
    Disposition, Header, HttpRequest, HttpResponse, IdentityHandler, JournalingHandler,
    ObservationSink, ObservedEvent, RequestHandler, VolumePolicy,
};

// The UTF-16 string type and ordinal-casing seam now live in the standalone
// `windows-text` crate (D16 charter; CHECKLIST M2/M3). Re-exported here for API
// continuity. `Win32OrdinalCasing` — the mandated production casing (D6/D8) — is
// Windows-only.
pub use windows_text::{OrdinalCasing, Utf16};
#[cfg(windows)]
pub use windows_text::Win32OrdinalCasing;

#[cfg(test)]
mod integration_tests;
