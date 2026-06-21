// Copyright (c) Microsoft Corporation.

//! Reserved single home for `unsafe` FFI (D13).
//!
//! This module is intentionally empty until the M2 milestone introduces the
//! `windows-sys` bindings, the RAII handle wrappers, and the production Win32
//! ordinal-casing implementation (D6/D8). Concentrating every `unsafe` call
//! here is what lets the rest of the crate remain memory-safe and auditable.
//!
//! When this module gains `unsafe` code, the crate-level `#![forbid(unsafe_code)]`
//! in `lib.rs` will be relaxed to `#![deny(unsafe_code)]` and this module alone
//! will carry `#[allow(unsafe_code)]`.
