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

---

## Milestone AJ-A — `api-journal` shared crate (foundation)

The single source of truth for the NDJSON record schema and JSON body-shape model,
depended on by both the shim (writer) and cartographer (reader). Pure data; no Win32.

- [x] AJ-A1: Scaffold `crates/api-journal` — Cargo.toml (workspace member, edition/rust-version `.workspace = true`, `serde` + `serde_json`), `src/lib.rs` with `#![forbid(unsafe_code)]` + copyright, seed `DESIGN-NOTES.md` recording the capture/offline architecture and the five confirmed decisions, and a `PLANS.md` pointing at this checklist. Add the crate to the root workspace `members`. Clean `cargo_build`.
- [x] AJ-A2: `BodyShape` model + inference — recursive shapes-only JSON shape (Null/Bool/Integer/Number/String/Array(elem)/Object{field→{shape,required}}/Union/Empty), `derive_shape(bytes, content_type) -> BodyShape` (JSON bodies; non-JSON → opaque `String`/`Empty`), and `merge(a, b)` with union + required-narrowing semantics. ≥10 unit tests + edges (empty body, non-JSON, deep nesting, null, mixed-type array → Union, object field present in only some samples → optional).
- [x] AJ-A3: `JournalRecord` schema — serde struct: `seam` (Inbound/Egress), `method`, egress authority (`scheme`/`host`/`port`), raw `path`, `query` params (name + value-shape), request `header_names` (+ literal values only for the content-negotiation safelist `Content-Type`/`Accept`), request/response `BodyShape`, response `status`, request/response content-types, `timestamp_ms`, `session_id`, `seq`. Unknown fields ignored on read (forward-compat). Unit tests (serde round-trip; unknown-field tolerance; both seams).
- [x] AJ-A4: NDJSON IO — `append_record(writer, &record)` (one compact JSON object per line) + `read_records(reader) -> (Vec<JournalRecord>, SkippedCount)` tolerant of blank/comment/malformed lines (skipped + counted, never panics). Unit tests (good stream, interleaved blank/garbage lines, truncated trailing line).
- [x] AJ-A5: Milestone integration — round-trip a few hundred synthesized records through a temp NDJSON file (write then read-back equality). Implicit end-of-milestone steps (clean build debug+release with zero warnings, in-scope tests, sync + push).

> **➡ CROSS-COMPONENT HANDOFF:** next work is in component `crates/windows-win32-shim` → `AJ-B` (shim capture wiring). This checklist continues there.

---

## Milestone AJ-B — Shim capture wiring (pilcfg + egress + inbound)

Component: `crates/windows-win32-shim` (depends on the now-landed `api-journal`).

> **⟸ CROSS-COMPONENT PREREQUISITE:** `crates/api-journal` milestone AJ-A must be landed first (provides `JournalRecord`, `BodyShape`, NDJSON IO).

- [x] AJ-B1: `.pilcfg` `api_journal` block — extend `pilcfg.rs` with `ApiJournalConfig { enabled: bool (default false), path: String (%VAR%-expanded), bodies: Shapes|Full|None (default Shapes), seams: { inbound: bool, egress: bool } (default both), max_body_bytes: usize }`. Tolerant parse, default-disabled, unknown members ignored (matching existing pilcfg policy). Unit tests.
- [x] AJ-B2: Journal sink — process-wide thread-safe append writer (Mutex-guarded), lazy file open on first record via real `std::fs` (the shim's own I/O is NOT aliased), per-line flush for crash-robustness across many machines, `%VAR%` path expansion via the existing expander, fail-soft (never break the host). Unit tests (temp-dir write, expansion, concurrent append).
- [x] AJ-B3: Egress journaling decorator — derive a `JournalRecord` (seam=Egress) from `EgressRequest`/`EgressResponse` honoring the `bodies` mode, write via the sink; compose into `build_egress_backing` only when `api_journal.enabled` and the egress seam is on. Unit tests with a fake egress surface (records reflect verb/authority/path/status/body-shape).
- [x] AJ-B4: Inbound (web.rs) journaling — capture inbound request/response bodies (as shapes) at the IIS web-host seam and emit a `JournalRecord` (seam=Inbound) via the sink when enabled and the inbound seam is on. Extend `web.rs` capture as needed. Unit tests.
- [ ] AJ-B5: Milestone integration — hermetic `tests/journal_capture.rs` integration test: build a real `ShimSession::from_config` from a `Pilcfg` with the `api_journal` block enabled (egress mode `buffer`, so an outbound POST is acknowledged synthetically with no network), drive BOTH seams — an outbound POST through the WinHTTP egress engine and an inbound GET through the per-request handler stack — then assert the shared NDJSON journal file holds one `Seam::Egress` and one `Seam::Inbound` record sharing one `session_id` with monotonic `seq`. (Re-planned from the originally-envisioned aliased-binary + live-`merriam` + IIS PowerShell harness: that path mainly re-proves link-time aliasing already covered by MW17/MW18 and is flaky/elevation-gated, whereas the journaling *wiring* — the new behavior — is proven deterministically here. The aliased/live proof remains available as the existing `egressrelayproof` harness.) Implicit end-of-milestone steps (clean build debug+release, shim tests, sync + push).

> **➡ CROSS-COMPONENT HANDOFF:** next work is in component `crates/cartographer` → `AJ-C` (OAS model + loader). This checklist continues there.

---

## Milestone AJ-C — `cartographer`: OAS model + loader + shape→schema

Component: `crates/cartographer` (new bin+lib; depends on `api-journal`).

> **⟸ CROSS-COMPONENT PREREQUISITE:** `crates/api-journal` AJ-A (record/shape types) must be landed. AJ-B is not strictly required to start AJ-C (cartographer can be developed against fixture journals), but AJ-B5's real journal feeds the AJ-E5 end-to-end proof.

- [ ] AJ-C1: Scaffold `crates/cartographer` — bin + lib, deps `api-journal` + `serde` + `serde_json` + a maintained YAML crate, copyright + `DESIGN-NOTES.md` + `PLANS.md`, workspace member. Introduce the single output-sink trait NOW (one `write` site abstraction per the repo "one output site" rule) so all later diagnostics/spec output route through it. Clean build.
- [ ] AJ-C2: OAS 3.1 model subset — serde types `Document` (openapi/info/servers?/paths/components?), `Info`, `PathItem`, `Operation` (operationId/summary?/parameters/requestBody?/responses), `Parameter` (name/in/required/schema), `RequestBody` (content map), `Response` (description/content), `Schema` (JSON-Schema-2020-12 subset: type incl. type-array nullability, properties, required, items, format, anyOf, `$ref`). Unit tests (serde round-trip of a hand-written doc).
- [ ] AJ-C3: Spec loader — ingest a file or directory of specs, JSON or YAML (auto-detected), tolerant with a diagnostic per unparseable spec (never panic). Unit tests with JSON + YAML fixtures.
- [ ] AJ-C4: `BodyShape` → OAS `Schema` renderer — map the api-journal shape model to a JSON-Schema-2020-12 `Schema` (Union → `anyOf`, null member → type-array, Object → properties+required, Array → items). Unit tests.
- [ ] AJ-C5: Milestone integration — load a fixture spec (both YAML and JSON forms), render sample shapes to schemas, round-trip write to YAML and JSON. Implicit end-of-milestone steps (clean build debug+release, in-scope tests, sync + push).

---

## Milestone AJ-D — `cartographer`: validation + diagnostics

Component: `crates/cartographer`.

- [ ] AJ-D1: Path matcher — match a concrete observed path against spec path templates (`{param}` segments), choosing the most-specific template; report no-match. Unit tests (literal vs templated, `/custom` vs `/custom/{word}`, trailing slash, ambiguous).
- [ ] AJ-D2: Diagnostic model + rendering — `Diagnostic { severity, code, location (path/method/status), message }` rendered through the output sink in `text` and `ndjson` report modes. Unit tests.
- [ ] AJ-D3: Validation rules — `UndocumentedPath`, `UndocumentedOperation` (method not in PathItem), `UndeclaredStatus`, `UndeclaredParameter` (query), `UndeclaredHeader` (excluding standard headers), `RequestSchemaMismatch`, `ResponseSchemaMismatch`, `TypeMismatch`. One unit test per rule.
- [ ] AJ-D4: Stream aggregation — validate a whole journal stream, deduplicating repeated identical violations with an observation count. Unit tests.
- [ ] AJ-D5: Milestone integration — fixture journals + a fixture spec produce a known, asserted diagnostic set (including a clean run with zero diagnostics). Implicit end-of-milestone steps (clean build debug+release, in-scope tests, sync + push).

---

## Milestone AJ-E — `cartographer`: synthesis/merge + CLI + end-to-end

Component: `crates/cartographer`.

- [ ] AJ-E1: Path-template inference — when a baseline spec exists, match observed paths to its templates first; for unmatched/empty-baseline paths, apply a conservative variable-segment heuristic (a segment under a shared parent taking many distinct values becomes `{segN}`). Unit tests (`/custom/cat` + `/custom/dog` → `/custom/{word}`; `/healthz` stays literal; `/custom` enumerate vs `/custom/{word}` item disambiguation).
- [ ] AJ-E2: Operation synthesis — per (template, method): synthesize query/header `Parameter`s from observed names + value-shapes, `RequestBody` from merged request shapes per media type, and `responses` per observed status with merged response-body schemas. Unit tests.
- [ ] AJ-E3: Spec merge — combine synthesized operations into an existing `Document`, preserving human-authored summaries/operationIds/descriptions and widening (not overwriting) schemas; `--overwrite` to replace instead. Unit tests (empty baseline = fresh synth; non-empty baseline = additive merge).
- [ ] AJ-E4: CLI — `cartographer --spec <path>... --journal <path>... --out <dir> --format yaml|json --report text|ndjson [--update] [--strict]`; default reads spec+journals and reports diagnostics; `--update` writes merged specs; all output via the sink; exit code nonzero under `--strict` when violations exist. Unit/CLI tests.
- [ ] AJ-E5: End-to-end — feed the AJ-B5 captured journal (real wordy + merriam traffic) into `cartographer --update` to emit OAS 3.1 YAML for both services plus a diagnostics report; assert the generated spec contains the expected paths/operations/status codes (`/healthz`, `/custom`, `/custom/{word}` GET/POST/DELETE, `/spellcheck`, `/anagram`, `/shared`). Implicit end-of-milestone steps (clean build debug+release, full in-scope tests, sync + push). On completion: mark the root `PLANS.md` entry completed and move this file to `COMPLETED-CHECKLIST.md`.

---

## Deferred follow-ups

Queued (not yet scheduled into a milestone); each records a decision whose implied work is
intentionally postponed.

- [ ] AJ-DEF-1: Full-body example capture (D-AJ-3). Add an optional literal example-body
  field to `api_journal::JournalRecord` and populate it at the egress/inbound seams when
  `.pilcfg` `api_journal.bodies` is `full`, so `cartographer` can emit OpenAPI `examples`.
  Until done, `BodyCapture::Full` is a documented alias of `Shapes`.
