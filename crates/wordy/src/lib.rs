// Copyright (c) Microsoft Corporation.

#![deny(unsafe_code)]

//! `wordy` — a shim-unaware Rust IIS native-module REST service.
//!
//! `wordy` is a deliberately ordinary third-party web application: a "shared
//! dictionary" service hosted as an IIS native module (and, later, run under
//! Hostable Web Core). Its purpose in this repository is to be a realistic proof
//! harness for the link-time host-call redirection developed in
//! [`windows-win32-shim`](../windows-win32-shim) — and a place to grow genuine
//! HWC business logic — **without** `wordy` itself knowing anything about that
//! isolation. See `DESIGN-NOTES.md` for the shim-unaware contract and
//! windows-win32-shim SHIM-D19 for the surrounding design.
//!
//! The crate is split into:
//! - [`words`] — the pure, safe, platform-independent word-domain core: the
//!   shared dictionary plus spell-check, regex enumeration, the anagram solver,
//!   and edit-distance suggestions.
//! - [`custom`] — the pure, safe, platform-independent custom-dictionary FS
//!   store: per-user / per-locale words kept as name-encoded marker files.
//! - [`routes`] — the pure, safe, platform-independent request → outcome logic.
//! - `iis` (Windows only) — the `#[allow(unsafe_code)]` native-module ABI
//!   boundary that bridges the IIS host into [`routes`]. It is a peer of
//!   windows-win32-shim's `mwinweb` and never depends on that crate.

pub mod custom;
pub mod routes;
pub mod words;

#[cfg(windows)]
#[allow(unsafe_code)]
mod iis;
