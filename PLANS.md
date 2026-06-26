# Repository-root plans

Tracks cross-cutting CHECKLIST.md files rooted at the repository-root source-component
(plans scoped to a single sub-component live in that component's own PLANS.md).

| Path to CHECKLIST.md | Status | Brief description | Design Notes |
|---|---|---|---|
| [CHECKLIST-apijournal.md](CHECKLIST-apijournal.md) | in progress | Auto-generate OpenAPI/Swagger specs from shim-observed API interactions: an `api-journal` shared schema + NDJSON capture wired into the win32 shim via `.pilcfg`, plus an offline `cartographer` tool that validates gathered journals against existing specs (diagnostics) and synthesizes/merges updated specs. | crates/api-journal/DESIGN-NOTES.md (planned) |
