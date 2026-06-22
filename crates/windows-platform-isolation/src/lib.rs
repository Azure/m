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
//! The first cut has no live/"direct" provider (D15): the safe core is
//! exercised entirely through synthetic in-memory data and tests. Persistence
//! (the shared artifact format, D5) and the live Win32 provider are later
//! milestones.
//!
//! See `DESIGN-NOTES.md` for the governing decisions (D1–D15).

#![forbid(unsafe_code)]

pub mod decorator;
pub mod error;
pub mod path;
pub mod registry;
pub mod serial;
pub mod surface;
pub mod tree;

pub use decorator::{Buffered, PassThrough};
pub use error::{RegistryError, Result};
pub use path::KeyPath;
pub use registry::{Registry, Session, WellKnownRoot};
pub use serial::load_registry_hive;
pub use surface::{Request, Response, Surface, TreeSurface};
pub use tree::{Hive, OverlayTree, ValueData, ValueType};

// The UTF-16 string type and ordinal-casing seam now live in the standalone
// `windows-text` crate (D16 charter; CHECKLIST M2/M3). Re-exported here for API
// continuity. `Win32OrdinalCasing` — the mandated production casing (D6/D8) — is
// Windows-only.
pub use windows_text::{OrdinalCasing, Utf16};
#[cfg(windows)]
pub use windows_text::Win32OrdinalCasing;

#[cfg(test)]
mod integration_tests;
