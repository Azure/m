// Copyright (c) Microsoft Corporation.

//! `cartographer` — the offline tool that turns journaled HTTP API interactions
//! into OpenAPI.
//!
//! The win32 shim journals observed request/response interactions as NDJSON (the
//! shared [`api_journal`] schema). Those journals are gathered off-machine and
//! handed to this tool, which:
//!
//! 1. loads the project's existing OpenAPI specs (if any),
//! 2. **validates** the observed interactions against them, reporting any
//!    deviation (an undocumented path, an undeclared status code, a body-schema
//!    mismatch, …) as a diagnostic, and
//! 3. **synthesizes** updated OpenAPI 3.1 specs from what was observed, merging
//!    into the existing specs rather than overwriting human-authored prose.
//!
//! The tool is pure data — no Win32, no `unsafe`. Every byte it emits flows
//! through a single [`OutputSink`] (the repository's "one output site" rule), so
//! the output target is separable from the code that produces content.
//!
//! Modules land across milestones AJ-C..AJ-E:
//! - `sink` (AJ-C1): the output abstraction.
//! - `model` (AJ-C2): the OpenAPI 3.1 document model.
//! - `format` (AJ-C3): reading and writing specs as JSON or YAML.
//! - `schema` (AJ-C4): rendering [`api_journal`] body shapes to OpenAPI schemas.
//! - validation (AJ-D) and synthesis (AJ-E) follow.

#![forbid(unsafe_code)]

pub mod diagnostics;
pub mod format;
pub mod model;
pub mod path;
pub mod schema;
pub mod sink;
pub mod validate;

pub use validate::{SpecIndex, validate_record};
pub use diagnostics::{
    Diagnostic, DiagnosticCode, Location, ReportFormat, Severity, render as render_diagnostics,
};
pub use path::{PathMatch, PathTemplate, best_match};
pub use schema::render_schema;
pub use format::{
    LoadError, LoadOutcome, LoadedSpec, SpecFormat, load_path, parse_auto, parse_document,
    serialize_document,
};
pub use model::{
    Components, Content, Document, Info, MediaType, Operation, Parameter, ParameterIn, PathItem,
    Paths, RequestBody, Response, ResponseHeader, Responses, Schema, SchemaType, Server, SimpleType,
};
pub use sink::{BufferSink, OutputSink, StdoutSink};
