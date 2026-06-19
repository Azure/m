# pil plans

| Path to CHECKLIST.md | Status | Brief description | Design Notes |
|---|---|---|---|
| [CHECKLIST.md](CHECKLIST.md) | in progress | **HWC isolation** (third surface, active): `iwebcore` engine surface surfaced through `mwin32` `mWebCore*` shims — Phase 1 (surface/null provider M-HWC-IFACE, live `LoadLibraryExW` provider M-HWC-DIRECT, decorator facets M-HWC-FACETS), Phase 2 (config materialization M-HWC-MATERIALIZE / opt-in module-scoped interception M-HWC-INTERCEPT), Phase 3 (`ihttp_listener` namespace redirection M-HWC-HTTP). **Phase 4** (OpenAPI/Swagger contract binding, D-HWC-8/D-HWC-9, in progress): YAML spec loader + internal model (M-HWC-CONTRACT-MODEL, **done**), `$ref` bundle resolution + media-typed (incl. XML) bodies (M-HWC-CONTRACT-REFS, **done**), `ihttp_contract` surface + null provider (M-HWC-CONTRACT-IFACE, **done**), validate-mode facet on the synthetic edge (M-HWC-CONTRACT-VALIDATE, **done**), example-driven traffic (M-HWC-CONTRACT-DRIVE); the `.pilcfg` binding is the `mwin32` M-HWC-CONTRACTCFG companion. **M-FS-STREAMS** tier-1 (redirection-backed file content, D17) is now active: content accessor `ifile::read_content` / `write_content` (1.1 / 1.2, the `mwin32` M-FS-CONTENT unblocker), then subtree binding (1.3) + namespace-mutation overlay (1.4); tier-2 ADS sub-namespace (M-FS-STREAMS-2) stays deferred | [DESIGN-NOTES.md](DESIGN-NOTES.md) (D9–D17, D-HWC-1…D-HWC-9) |

Completed plans are recorded in [COMPLETED-PLANS.md](COMPLETED-PLANS.md); completed
checklist groups in [COMPLETED-CHECKLIST.md](COMPLETED-CHECKLIST.md).

