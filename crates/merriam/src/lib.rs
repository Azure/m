// Copyright (c) Microsoft Corporation.

//! `merriam` — a REST dictionary-store service.
//!
//! `merriam` owns a per-`(locale, user)` **custom dictionary** on disk and
//! exposes it over a small REST surface. It is the dependent service the
//! validation tier (windows-win32-shim **SHIM-D23**) carves out of `wordy`:
//! `wordy` keeps its CPU word work and **relays** the custom-dictionary
//! operations to `merriam` over WinHTTP, which becomes the egress the shim's
//! WinHTTP seam (MW17) isolates.
//!
//! The crate is layered like `wordy`:
//! - [`Store`] — the on-disk dictionary. Each `(locale, user)` is **one
//!   newline-delimited word-list file** read and written through
//!   [`windows_file_io`] (native async overlapped Win32 I/O). This is a
//!   *content* store, in contrast to `wordy`'s name-encoded empty files — it is
//!   what exercises the async overlapped read/write path.
//! - the dispatch core (`routes`, MW18-2.2) — a host-agnostic request →
//!   response router mirroring `wordy`'s custom-dictionary routes 1:1.
//! - the http.sys listener edge (MW18-2.3) — the HTTP Server API inbound.
//!
//! This crate is Windows-only (its store builds on the Windows-only
//! [`windows_file_io`]); on other targets it compiles to an empty library.

#![deny(unsafe_code)]

#[cfg(windows)]
mod store;

#[cfg(windows)]
pub use store::{Store, StoreError, StoreResult};

#[cfg(windows)]
mod routes;

#[cfg(windows)]
pub use routes::{
    CONTENT_TYPE_JSON, DEFAULT_LOCALE, DEFAULT_USER, HttpRequest, HttpResponse, LOCALE_HEADER,
    Outcome, STATUS_BAD_REQUEST, STATUS_INTERNAL_ERROR, STATUS_OK, Service, USER_HEADER, path_of,
    query_of,
};

#[cfg(windows)]
#[allow(unsafe_code)]
mod http_sys;

#[cfg(windows)]
pub use http_sys::{ERROR_ACCESS_DENIED, Server};
