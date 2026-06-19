# CHECKLIST — HTTP contract recorder (derive OpenAPI YAML from observed traffic)

Feature scope (PIL side of the wire-capture lifecycle demo): observe clean
request/response crossings and **emit** an OpenAPI YAML spec, then load that spec
back through the existing contract document loader so it can drive validate mode.

This is the "derive the contracts" capability the demo needs; it does not exist
today (the contract layer only *parses* specs, it cannot *emit* them).

Related design: see DESIGN-NOTES.md (contract surface, D-HWC-8/9). The mwin32 wire
capture that feeds this recorder lives in
[`src/Windows/libraries/mwin32/CHECKLIST-wirecapture.md`](../../Windows/libraries/mwin32/CHECKLIST-wirecapture.md).

## Milestone M-REC — recorder + OpenAPI YAML emitter

- [x] **REC-1**: OpenAPI model → YAML emitter. Add an internal
  `emit_openapi_yaml(openapi_document const&) -> std::string` (yaml-cpp `Emitter`)
  that serializes operations (method, path template, parameters, request body
  schema, status→response map) into a loadable OpenAPI 3.0 document. Unit test:
  round-trip a known in-repo spec (load → emit → reload) and assert structural
  equality on the operation set.

- [x] **REC-2**: Body-shape inference. Given one or more observed JSON request/
  response bodies for an operation, infer a minimal JSON Schema (object with
  `type` per field; `required` = intersection of keys seen across samples; arrays
  inferred from element shape). Non-JSON bodies recorded by media type only.
  Unit tests for object/array/scalar/empty and the required-key intersection rule.

- [x] **REC-3**: `ihttp_contract_recorder` (internal): `observe_request(method,
  path, headers, body)`, `observe_response(method, path, status, headers, body)`;
  correlates by method+path, accumulates into an `openapi_document`, dedupes
  operations, merges inferred body schemas (REC-2) and observed status codes.
  `emit_spec() -> std::string` returns the YAML (REC-1). Unit tests covering
  multi-operation, multi-status accumulation and idempotent re-observation.

- [ ] **REC-4**: Public façade `make_http_contract_recorder(...)` on the PIL
  contract surface, plus a closing-the-loop test: feed the recorder a set of
  synthetic crossings, emit the spec, load it with `ihttp_contract::load`, and
  confirm the resulting document `validate_request`/`validate_response` ACCEPTS
  the clean crossings and REJECTS a deliberately mutated one.
  > **➡ CROSS-COMPONENT HANDOFF:** next work is in component
  > `src/Windows/libraries/mwin32` → `M-WIRECAP-CFG` → `WC-5` (wire record mode to
  > this recorder). See
  > [`CHECKLIST-wirecapture.md`](../../Windows/libraries/mwin32/CHECKLIST-wirecapture.md).

### Implicit end-of-milestone steps (not work items)
Clean debug+release build of m_pil (zero warnings), run PIL contract tests, sync
with origin.
