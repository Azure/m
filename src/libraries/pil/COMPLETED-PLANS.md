# pil completed plans

| Path to CHECKLIST.md | Completion Date | Brief description | Design Notes |
|---|---|---|---|
| CHECKLIST-contract-recorder.md (moved to COMPLETED-CHECKLIST.md) | 2026-06-19 | **Contract recorder** (M-REC): observe clean request/response crossings and **emit** an OpenAPI YAML spec (OpenAPI model→YAML emitter + body-shape inference + internal `http_contract_recorder` + public `make_http_contract_recorder` façade), the "derive the contracts" capability for the mwin32 wire-capture demo. Close-the-loop test loads the derived spec through `make_http_contract_provider` and confirms validate accepts clean / rejects mutated crossings. Hands off to mwin32 M-WIRECAP-CFG WC-5. | [DESIGN-NOTES.md](DESIGN-NOTES.md) |
| [CHECKLIST.md](CHECKLIST.md) | 2026-06-15 | PIL decorator scenarios: buffered sealed snapshot (M-PS), logging-off-persistence (M-LOG-OUT/FLOAT), journaling replay (M-JOURNAL), buffered delete_tree (M-BUFTREE), fault injection (M-FAULT), controllable mock ikey (M-PS-MOCK), legacy save-path cleanup (M-CLEANUP/PERSIST-1) | [DESIGN-NOTES.md](DESIGN-NOTES.md) |
