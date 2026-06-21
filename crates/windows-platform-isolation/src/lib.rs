//! Windows platform isolation — registry surface (a Rust reimplementation of the
//! C++ Platform Isolation Library, PIL).
//!
//! This crate is the **safe half** (D13): all stateful logic — the overlay /
//! copy-on-write tree, the reified operation seam, the decorator stack, and the
//! typed facade — is memory-safe Rust under `#![forbid(unsafe_code)]`. The
//! single future home for `unsafe` FFI is the reserved [`ffi`] module, which is
//! empty until the M2 FFI-leaf milestone.
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
pub mod ffi;
pub mod path;
pub mod registry;
pub mod surface;
pub mod tree;
pub mod wstr;

pub use decorator::{Buffered, PassThrough};
pub use error::{RegistryError, Result};
pub use path::KeyPath;
pub use registry::{Registry, Session, WellKnownRoot};
pub use surface::{Request, Response, Surface, TreeSurface};
pub use tree::{Hive, OverlayTree, ValueData, ValueType};
pub use wstr::{OrdinalCasing, Utf16};
