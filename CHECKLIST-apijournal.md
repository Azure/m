# CHECKLIST — Auto-generated OpenAPI from shim-observed API interactions

Feature: the win32 shim, when enabled via `.pilcfg`, journals observed HTTP API
interactions (egress + IIS inbound seams) as NDJSON. Journals are gathered off-machine
and fed to an offline `cartographer` tool that (1) validates them against the project's
existing OpenAPI specs, emitting diagnostics for violations, and (2) synthesizes/merges
updated OpenAPI 3.1 specs from what was observed.

Confirmed decisions (2026-06-25): tool crate = `cartographer`; spec format = read JSON or
YAML, default-write YAML; body capture default = shapes-only (JSON schema skeleton, no
literal scalar values); first-cut seams = egress (WinHTTP MW17) + IIS inbound (web.rs);
baseline = start empty, synthesize fresh.

ID scheme: `AJ-<milestone><n>`; decimal sub-steps (`AJ-A1.1`).

**Milestones AJ-A through AJ-E are complete** (api-journal shared crate; shim capture
wiring; cartographer OAS model + loader + shape→schema; validation + diagnostics;
synthesis/merge + CLI + end-to-end). Their items have been moved to
[`COMPLETED-CHECKLIST.md`](COMPLETED-CHECKLIST.md).

---

## Milestone AJ-F — Full-body example capture (AJ-DEF-1)

Make `.pilcfg` `api_journal.bodies: full` capture a literal example body (in addition to
the shapes-only skeleton) so `cartographer` can emit OpenAPI `example`s. Resolves D-AJ-3;
until landed, `BodyCapture::Full` is a documented alias of `Shapes`. Privacy note: examples
are literal user data and are captured **only** under the opt-in `full` mode.

- [x] AJ-DEF-1.1: api-journal — add optional `request_body_example` / `response_body_example` (`Option<serde_json::Value>`, serde `default` + skip-when-`None`, forward-compatible) to `JournalRecord`, and a pure `derive_example(bytes, content_type) -> Option<serde_json::Value>` (parse JSON bodies → `Value`; `None` for empty or non-JSON). Unit tests (JSON object/array/scalar → example; empty/non-JSON → `None`; record serde round-trip with and without examples).
- [x] AJ-DEF-1.2: shim — add `JournalSink::body_example(bytes, content_type) -> Option<serde_json::Value>` returning `Some` only under `BodyCapture::Full`; populate the example fields in the egress and inbound record builders (the shape is still derived as today). Unit tests (Full captures example; Shapes/None do not). Update `SHIM-D24` and `api-journal` D-AJ-3 to reflect that `Full` now captures examples.
- [ ] AJ-DEF-1.3: cartographer — add `example: Option<serde_json::Value>` to the OAS `MediaType` model (serde skip-when-`None`), and have `synthesize` set a representative captured example per media type on request bodies and per-status responses. Unit tests (synth emits the observed example; serde round-trip of a doc carrying an example).
- [ ] AJ-DEF-1.4: end-to-end + close — integration test: a `full`-mode journal flows through `cartographer --update` and the emitted OpenAPI carries the observed `example`(s). Flip D-AJ-3 to implemented. Implicit end-of-milestone steps (clean build debug+release across api-journal + windows-win32-shim + cartographer, in-scope tests, sync + push). On completion move this milestone to `COMPLETED-CHECKLIST.md`.
