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
//! To see capture + synthesis end to end, run the bundled example, which writes a
//! sample `wordy` journal alongside the OpenAPI spec synthesized from it (see the
//! crate README for details):
//!
//! ```text
//! cargo run -p cartographer --example wordy -- <out_dir>
//! ```
//!
//! Modules land across milestones AJ-C..AJ-E:
//! - `sink` (AJ-C1): the output abstraction.
//! - `model` (AJ-C2): the OpenAPI 3.1 document model.
//! - `format` (AJ-C3): reading and writing specs as JSON or YAML.
//! - `schema` (AJ-C4): rendering [`api_journal`] body shapes to OpenAPI schemas.
//! - validation (AJ-D) and synthesis (AJ-E) follow.

#![forbid(unsafe_code)]

pub mod cli;
pub mod diagnostics;
pub mod environment;
pub mod format;
pub mod infer;
pub mod merge;
pub mod model;
pub mod path;
pub mod schema;
pub mod sink;
pub mod synth;
pub mod validate;

pub use infer::TemplateSet;
pub use environment::{
    Actor, Actors, Binding, Channel, Channels, ContractRef, Environment, Observed, Role, RolePart,
    Roles, Security, Transport,
};
pub use merge::merge;
pub use synth::synthesize;
pub use cli::{Args, parse_args, run};
pub use validate::{SpecIndex, validate_record, validate_stream};
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
