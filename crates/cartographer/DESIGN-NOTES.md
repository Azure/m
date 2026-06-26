# Design notes — `cartographer`

`cartographer` is the offline, read-side counterpart to the win32 shim's
journaling capture. It consumes the shared `api-journal` NDJSON, validates it
against the project's OpenAPI specs, and synthesizes updated OpenAPI 3.1
documents. It is a pure-data tool: no Win32, no `unsafe`.

## Specified behavior (we own it; deps merely satisfy it)

We define cartographer's behavior and choose dependencies that satisfy it
(Design Autonomy):

- **OpenAPI model and serialization.** The OpenAPI 3.1 document model and its
  mapping to JSON and YAML are owned here. We use `serde` / `serde_json` (and, at
  AJ-C3, a YAML library) because their behavior matches our specification — the
  spec shape is ours, not "whatever the library emits." If a library diverges, we
  wrap or replace it.
- **Output.** Every byte the tool emits — diagnostics and generated spec text —
  flows through a single [`OutputSink`](src/sink.rs) (the repository "one output
  site" rule). The default standard-output implementation is `StdoutSink`; tests
  use `BufferSink`.

## Confirmed feature decisions (2026-06-25)

- **Spec format:** cartographer reads JSON *or* YAML and default-writes YAML
  (selectable via `--format`). YAML support and the chosen YAML library land at
  AJ-C3 where they are first used.
- **Baseline:** the project starts with no OpenAPI specs; cartographer synthesizes
  fresh, and once specs exist, validates against and merges into them.
- **OAS version:** OpenAPI 3.1 (JSON Schema 2020-12 alignment: type arrays for
  nullability rather than `nullable`; `examples` arrays; no `format: binary`).

## D-CART-2 — YAML backend and schema rendering (AJ-C)

- **YAML library.** `serde_yaml_ng` (the maintained drop-in successor to the
  archived `serde_yaml`, sharing the same `unsafe-libyaml` backend) realizes our
  YAML read/write. The read/write contract is owned here (`format` module): read
  JSON *or* YAML (extension-detected, content-sniffed otherwise), default-write
  YAML; loading a directory is tolerant (one `LoadError` per bad file, never an
  abort). If the library diverges from this contract we wrap or replace it.
- **Shape → schema (`schema` module).** A body [`BodyShape`] renders to a
  JSON-Schema-2020-12 [`Schema`]: scalars → `type` tokens; objects → `properties`
  + `required`; arrays → `items`; a union of exactly `null` + one alternative →
  a nullable `type` array (e.g. `["string", "null"]`), and richer unions → `anyOf`.
  `Empty`/`Unknown` render to no schema (the caller omits the slot); `Opaque`
  (non-JSON) renders to a described `string`.

## Decision index

- **D-CART-1** — Single output sink abstraction (`OutputSink`) is introduced at
  crate creation, before any output site, so diagnostics and spec emission share
  one retargetable destination.
- **D-CART-2** — `serde_yaml_ng` for YAML; the read/write contract and the
  shape→schema rendering rules (nullable type-array vs `anyOf`) are owned here.
