# Design notes — `api-journal`

This crate is the shared on-disk contract between the win32 shim's capture seams (writer)
and the `cartographer` OpenAPI tool (reader). It is intentionally a pure-data leaf: no
Win32, no `unsafe`, no I/O beyond plain readers/writers.

## Why a separate crate (D-AJ-1)

The capture half lives in `crates/windows-win32-shim` and the synthesis half lives in
`crates/cartographer`. Neither should depend on the other (the shim must not pull in a CLI
tool; the offline tool must not pull in the Win32 shim). Factoring the NDJSON record schema
and the body-shape model into this leaf crate gives both halves one definition to share, so
the wire format cannot drift between writer and reader.

## Specified behavior (we own it; deps merely satisfy it)

- **On-disk format is newline-delimited JSON (NDJSON):** exactly one compact JSON object per
  line, one line per observed request/response interaction. We use `serde` + `serde_json` to
  realize this format because their behavior matches our specification; the format is ours,
  not "whatever serde does." Reading is *tolerant*: blank, comment, and malformed lines are
  skipped and counted, never fatal, because journals are gathered from many machines and may
  be concatenated or truncated in transit.
- **Forward compatibility:** readers ignore unknown record fields so a newer shim can add
  fields without breaking an older reader.

## Confirmed feature decisions (2026-06-25)

These were agreed with the repository owner and drive the schema:

1. **Tool crate name:** `cartographer` (the offline reader/synthesizer).
2. **Spec format:** cartographer reads JSON *or* YAML and default-writes YAML. (Not this
   crate's concern, but recorded for context.)
3. **Body capture default = shapes-only.** A request/response body is journaled as a
   [`shape`] — a JSON schema skeleton (field names, JSON types, nesting, array element
   shape) with **no literal scalar values**. This lets cartographer infer schemas without
   exporting user data. `Full` (truncated bytes) and `None` (metadata only) remain
   selectable per-machine via `.pilcfg`, but `Shapes` is the default.
4. **First-cut capture seams:** egress (WinHTTP, the shim's MW17 seam) and IIS inbound
   (the `web.rs` seam). http.sys server-side capture is a possible later addition.
5. **Baseline:** the project starts with no OpenAPI specs; cartographer synthesizes fresh
   and, once specs exist, validates against and merges into them.

## Metadata capture policy (D-AJ-2)

Shapes-only governs *bodies*. For the surrounding metadata:

- **Path** is captured literally — cartographer needs concrete paths to infer templates
  (e.g. `/custom/cat` + `/custom/dog` → `/custom/{word}`). A future stricter mode may redact
  variable path segments.
- **Query parameters** capture the parameter *name* and a value *shape/type*, not literal
  values.
- **Headers** capture header *names*; literal header values are captured only for a small
  content-negotiation safelist (`Content-Type`, `Accept`) because the media type is needed
  to key request/response schemas. Authorization, cookie, and identity header values are
  never captured literally.

## Decision index

- **D-AJ-1** — Separate shared leaf crate so shim and cartographer share one wire format
  without depending on each other.
- **D-AJ-2** — Shapes-only applies to bodies; path is literal (templating needs it), query
  names + value-shapes, header names + content-negotiation values only.
