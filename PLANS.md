# Repository-root plans

Tracks cross-cutting CHECKLIST.md files rooted at the repository-root source-component
(plans scoped to a single sub-component live in that component's own PLANS.md).

| Path to CHECKLIST.md | Status | Brief description | Design Notes |
|---|---|---|---|
| [CHECKLIST-apijournal.md](CHECKLIST-apijournal.md) | in progress | Auto-generate OpenAPI/Swagger from shim-observed API interactions. **Milestones AJ-A..AJ-E complete and pushed** (api-journal shared crate; shim `.pilcfg` capture + egress/inbound decorators; `cartographer` OAS model, loader, validation, synthesis/merge, CLI, end-to-end). Completed items archived in [COMPLETED-CHECKLIST.md](COMPLETED-CHECKLIST.md); only the deferred `AJ-DEF-1` (full-body example capture) remains, parked. | crates/api-journal/DESIGN-NOTES.md; crates/cartographer/DESIGN-NOTES.md; crates/windows-win32-shim/DESIGN-NOTES.md (SHIM-D24) |
