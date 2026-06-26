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
[`COMPLETED-CHECKLIST.md`](COMPLETED-CHECKLIST.md). Only the deferred follow-up below
remains.

---

## Deferred follow-ups

Queued (not yet scheduled into a milestone); each records a decision whose implied work is
intentionally postponed.

- [ ] AJ-DEF-1: Full-body example capture (D-AJ-3). Add an optional literal example-body
  field to `api_journal::JournalRecord` and populate it at the egress/inbound seams when
  `.pilcfg` `api_journal.bodies` is `full`, so `cartographer` can emit OpenAPI `examples`.
  Until done, `BodyCapture::Full` is a documented alias of `Shapes`.
