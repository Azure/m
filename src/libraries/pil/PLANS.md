# pil plans

| Path to CHECKLIST.md | Status | Brief description | Design Notes |
|---|---|---|---|
| [CHECKLIST.md](CHECKLIST.md) | in progress | **HWC isolation** (third surface) is **complete** (Phases 1–4, D-HWC-1…D-HWC-11) and migrated to [COMPLETED-CHECKLIST.md](COMPLETED-CHECKLIST.md): `iwebcore` engine surface + live `LoadLibraryExW` provider + decorator facets, config materialization / module-scoped interception, `ihttp_listener` namespace redirection, and the full OpenAPI/Swagger contract layer (loader/refs/`ihttp_contract`/validate/drive/expose/edge) plus the activatable in-process synthetic-HTTP engine + `isynthetic_http_edge` submit/observe seam (M-HWC-ENGINE-EDGE) that unblocked `mwin32` M-HWC-CONTRACTCFG-7. **Remaining active work: filesystem surface** — **M-FS-STREAMS** tier-1 (redirection-backed file content, D16/D17: `ifile::read_content`/`write_content`, subtree binding, namespace-mutation overlay) is **done**; only tier-2 ADS sub-namespace (M-FS-STREAMS-2) stays **deferred**. Completed FS siblings M-FS-MONITOR-REDIR + M-FS-SHORTNAME are retained in the checklist as context until the FS surface is fully closed | [DESIGN-NOTES.md](DESIGN-NOTES.md) (D9–D17, D-HWC-1…D-HWC-11) |

Completed plans are recorded in [COMPLETED-PLANS.md](COMPLETED-PLANS.md); completed
checklist groups in [COMPLETED-CHECKLIST.md](COMPLETED-CHECKLIST.md).

