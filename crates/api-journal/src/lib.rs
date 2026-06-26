// Copyright (c) Microsoft Corporation.

//! `api-journal` — the shared, platform-independent schema for journaled HTTP API
//! interactions.
//!
//! The win32 shim, when enabled via its `.pilcfg`, observes HTTP traffic at two seams
//! (outbound WinHTTP egress and IIS inbound) and appends one [NDJSON] record per
//! interaction to a journal file. Those journals are gathered off-machine and fed to the
//! `cartographer` tool, which validates them against the project's existing OpenAPI specs
//! and synthesizes updated ones.
//!
//! This crate is the single source of truth for that on-disk format so the writer (shim)
//! and the reader (cartographer) can never drift. It carries no Win32 dependency and no
//! `unsafe`; it is pure data plus (de)serialization.
//!
//! Privacy: body capture defaults to *shapes only* — a JSON schema skeleton (field names,
//! types, nesting) with no literal scalar values — so journals describe an API's structure
//! without exporting user data. See [`DESIGN-NOTES.md`](../DESIGN-NOTES.md).
//!
//! Modules are introduced across milestone AJ-A:
//! - `shape` (AJ-A2): the body-shape model and inference.
//! - `record` (AJ-A3): the [`JournalRecord`](record) schema.
//! - `ndjson` (AJ-A4): tolerant NDJSON read/write helpers.
//!
//! [NDJSON]: https://github.com/ndjson/ndjson-spec

#![forbid(unsafe_code)]
