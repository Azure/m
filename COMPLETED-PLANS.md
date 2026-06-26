# Completed plans (repository root)

Cross-cutting CHECKLISTs (rooted at the repository-root source-component) whose every item
is complete. Their items are archived in [COMPLETED-CHECKLIST.md](COMPLETED-CHECKLIST.md).

| Path to CHECKLIST.md | Completion Date | Brief description | Design Notes |
|---|---|---|---|
| CHECKLIST-apijournal.md (archived) | 2026-06-25 | Auto-generate OpenAPI/Swagger from shim-observed API interactions: an `api-journal` shared NDJSON schema; shim `.pilcfg` capture at the egress + IIS-inbound seams (shapes-only by default, full-body examples under `bodies: full`); and an offline `cartographer` tool that validates gathered journals against existing specs and synthesizes/merges OpenAPI 3.1 specs (CLI, JSON/YAML). Milestones AJ-A..AJ-E + AJ-F. | crates/api-journal/DESIGN-NOTES.md; crates/cartographer/DESIGN-NOTES.md; crates/windows-win32-shim/DESIGN-NOTES.md (SHIM-D24) |
