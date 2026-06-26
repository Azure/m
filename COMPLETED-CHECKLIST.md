# Completed checklists (repository root)

Append-only archive of completed checklist groups rooted at the repository-root
source-component. Newest groups at the bottom.

## Moved 2026-06-25 — Auto-generated OpenAPI from shim-observed API interactions (milestones AJ-A..AJ-E)

Feature: the win32 shim, when enabled via `.pilcfg`, journals observed HTTP API
interactions (egress + IIS inbound seams) as NDJSON. Journals are gathered off-machine and
fed to an offline `cartographer` tool that (1) validates them against the project's existing
OpenAPI specs, emitting diagnostics for violations, and (2) synthesizes/merges updated
OpenAPI 3.1 specs from what was observed. All five milestones are complete and pushed; the
only remaining item is the deferred `AJ-DEF-1` (full-body example capture), tracked in
`CHECKLIST-apijournal.md`.

### Milestone AJ-A — `api-journal` shared crate (foundation)

- [x] AJ-A1: Scaffold `crates/api-journal` (workspace member, `#![forbid(unsafe_code)]`, serde + serde_json, DESIGN-NOTES + PLANS). Clean build.
- [x] AJ-A2: `BodyShape` model + inference (shapes-only JSON shape; `derive`; `merge` with union/widening/optional semantics). 18 tests.
- [x] AJ-A3: `JournalRecord` schema (seam/method/authority/path/query value-shapes/header names + content-negotiation values/body shapes/status/timestamps; forward-compatible). Tests.
- [x] AJ-A4: NDJSON IO (`write_record` + tolerant `read_records` skipping blank/comment/malformed lines, `ReadStats`). Tests.
- [x] AJ-A5: Milestone integration — 500-record temp-file NDJSON round-trip. Clean build debug+release, pushed.

### Milestone AJ-B — Shim capture wiring (pilcfg + egress + inbound)

Component: `crates/windows-win32-shim` (depends on `api-journal`).

- [x] AJ-B1: `.pilcfg` `api_journal` block — `ApiJournalConfig { enabled, path (%VAR%), bodies (Shapes|Full|None), seams {inbound,egress}, max_body_bytes }`, default-disabled, tolerant parse. Tests.
- [x] AJ-B2: Journal sink — process-wide thread-safe lazy-open append writer via real `std::fs` (not aliased), fail-soft, stamps session_id/seq/timestamp. Tests.
- [x] AJ-B3: Egress journaling decorator — `JournalingEgress` derives a `Seam::Egress` record from `EgressRequest`/`EgressResponse`; composed into the session egress stack. Tests.
- [x] AJ-B4: Inbound (web.rs) journaling — `JournalingHandler` `RequestHandler` decorator emits `Seam::Inbound` records with body shapes; wired into `WebState::build_handler`. Tests.
- [x] AJ-B5: Milestone integration — hermetic `tests/journal_capture.rs` drives a real `ShimSession` (api_journal enabled, egress buffer mode) through both seams; asserts one egress + one inbound record share a session_id with monotonic seq. (Re-planned from the aliased-binary + live-merriam + IIS PowerShell harness — aliasing already proven by MW17/MW18; the journaling wiring is the new behavior, proven deterministically here. `egressrelayproof` remains for the aliased/live path.) `SHIM-D24`. Clean build debug+release, pushed.

### Milestone AJ-C — `cartographer`: OAS model + loader + shape→schema

Component: `crates/cartographer` (new bin+lib; depends on `api-journal`).

- [x] AJ-C1: Scaffold `crates/cartographer` (bin+lib, `OutputSink`/`StdoutSink`/`BufferSink` "one output site" abstraction, DESIGN-NOTES + PLANS). Clean build.
- [x] AJ-C2: OAS 3.1 model subset (Document/Info/PathItem/Operation/Parameter/RequestBody/Response/Schema; type-array nullability, anyOf, spec renames, tolerant load). Tests.
- [x] AJ-C3: Spec loader/format (`serde_yaml_ng` for YAML; read JSON|YAML file/dir, tolerant `LoadError`; serialize). Tests.
- [x] AJ-C4: `BodyShape` → OAS `Schema` renderer (scalars/object/array/union; nullable type-array vs anyOf; opaque→string). Tests.
- [x] AJ-C5: Milestone integration — render shapes into a Document, round-trip JSON+YAML, load both from a directory. `D-CART-2`. Clean build debug+release, pushed.

### Milestone AJ-D — `cartographer`: validation + diagnostics

Component: `crates/cartographer`.

- [x] AJ-D1: Path matcher — `PathTemplate`/`PathMatch`, `{param}` matching, most-specific `best_match`. Tests.
- [x] AJ-D2: Diagnostic model + rendering — Severity/DiagnosticCode/Location/Diagnostic, text + ndjson via the sink. Tests.
- [x] AJ-D3: Validation rules — UndocumentedPath/Operation, UndeclaredStatus/Parameter/Header, Request/ResponseSchemaMismatch, TypeMismatch, with shape-vs-schema body conformance. Tests per rule.
- [x] AJ-D4: Stream aggregation — `validate_stream` dedups identical findings, sums counts, deterministic order. Tests.
- [x] AJ-D5: Milestone integration — spec YAML + journal NDJSON from files → asserted diagnostic set; clean run = none. Clean build debug+release, pushed.

### Milestone AJ-E — `cartographer`: synthesis/merge + CLI + end-to-end

Component: `crates/cartographer`.

- [x] AJ-E1: Path-template inference — `TemplateSet`; spec-driven match first, conservative trie heuristic collapses non-top-level ≥2-leaf siblings to a generic `{id}`. Tests.
- [x] AJ-E2: Operation synthesis — `synthesize`: path/query/header parameters, merged request body + per-status responses from observed shapes; required when seen on every observation. Tests.
- [x] AJ-E3: Spec merge — additive prose-preserving by default (add new paths/operations/statuses/params, keep human schemas); `--overwrite` replaces structure keeping prose. Tests.
- [x] AJ-E4: CLI — hand-rolled `--spec/--journal/--out/--format/--report/--update/--overwrite/--strict`; validate + report, `--update` writes the merged spec; exit 0/1/2; all output via the sink. Tests.
- [x] AJ-E5: End-to-end — a merriam+wordy journal → `cartographer --update` → OAS 3.1 YAML covering `/healthz`, `/custom`, `/custom/{id}` GET/POST/DELETE, `/spellcheck`, `/anagram`, `/shared`; the synthesized spec re-validates its own journal with zero findings. `D-CART-3`. Clean build debug+release, pushed. (Note: the heuristic synthesizes `/custom/{id}`, a human-refinable placeholder, rather than `/custom/{word}`.)
