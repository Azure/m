// Copyright (c) Microsoft Corporation.

//! Safe Rust wrapper over the Windows thread pool API.
//!
//! Rust parity with the C++ `m::threadpool` library
//! (`src/libraries/threadpool/`): submit work, timers, and periodic timers to
//! the OS thread pool without dedicating threads. See `DESIGN-NOTES.md` for the
//! design decisions (TP-D1..TP-D4).
//!
//! All `unsafe` is quarantined in the internal [`ffi`] module (TP-D4); the rest
//! of the crate is `deny(unsafe_code)`. The crate binds the raw Win32 entry
//! points through `windows-sys` (TP-D1) and owns its own safe abstraction.
//!
//! This crate is Windows-only; on other platforms it compiles to nothing.

#![deny(unsafe_code)]

#[cfg(windows)]
mod error;

#[cfg(windows)]
#[allow(unsafe_code)]
mod ffi;

#[cfg(windows)]
pub use error::{ThreadPoolError, ThreadPoolResult};
