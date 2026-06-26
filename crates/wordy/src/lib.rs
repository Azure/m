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
//! - [`identity`] — the caller identity ([`Principal`](identity::Principal)).
//! - [`store`] — the [`CustomStore`](store::CustomStore) trait the routes depend
//!   on (with an in-memory test store); production uses the `merriam` relay.
//! - [`custom`] — a filesystem [`CustomStore`](store::CustomStore) backing
//!   (name-encoded marker files), used as the selectable FS store for the
//!   filesystem-isolation proof (windows-win32-shim MW15).
//! - [`routes`] — the pure, safe, platform-independent request → outcome logic,
//!   generic over the [`CustomStore`](store::CustomStore).
//! - `relay` (Windows only) — the WinHTTP [`MerriamClient`](relay::MerriamClient)
//!   relay + its [`CustomStore`](store::CustomStore) adapter (WD-D13).
//! - `iis` (Windows only) — the `#[allow(unsafe_code)]` native-module ABI
//!   boundary that bridges the IIS host into [`routes`]. It is a peer of
//!   windows-win32-shim's `mwinweb` and never depends on that crate.

pub mod custom;
pub mod identity;
pub mod routes;
pub mod store;
pub mod words;

#[cfg(windows)]
#[allow(unsafe_code)]
mod winhttp;

#[cfg(windows)]
pub mod relay;

#[cfg(windows)]
#[allow(unsafe_code)]
mod iis;
