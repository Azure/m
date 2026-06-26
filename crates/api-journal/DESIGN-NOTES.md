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
- **D-AJ-3** — The record is shapes-only by default; `BodyCapture::Full` additionally
  captures a literal example body. Implemented in AJ-DEF-1: `JournalRecord` carries optional
  `request_body_example` / `response_body_example` (`serde_json::Value`), populated by the
  shim from `derive_example` only under `full` mode; `cartographer` emits these as OpenAPI
  `example`s.
- **D-AJ-4** — Principal identities and likely-PII request data must be *tokenized* (token in
  the journal; token→identity map in a separate, eventually *encrypted*, opt-in-to-decrypt
  sidecar); downstream (cartographer, D-CART-4) must not re-leak. Policy only; implementation
  deferred and queued in [`../../CHECKLIST-pii-tokenization.md`](../../CHECKLIST-pii-tokenization.md).

## D-AJ-3 — `BodyCapture::Full` captures a literal example body (implemented, AJ-DEF-1)

The `.pilcfg` `api_journal.bodies` option offers `shapes` (default), `full`, and `none`.
`none` is honored faithfully (body shape recorded as `Unknown`) and `shapes` is the design
center. `full` additionally retains a literal example body so cartographer can emit OpenAPI
`example`s: `JournalRecord` carries optional `request_body_example` / `response_body_example`
(`Option<serde_json::Value>`), and `derive_example` parses JSON bodies to a value. The shim
populates these only under `full` mode (examples are literal user data); cartographer's
`synthesize` sets a representative example per media type on request bodies and responses.
`JournalRecord` is consequently not `Eq` (arbitrary JSON is not `Eq`).

## D-AJ-4 — PII tokenization + encrypted identity map (policy; implementation deferred)

Captured data that identifies a *principal* — and, more broadly, any request data that is a
likely PII candidate — must not appear in the journal as plaintext. D-AJ-2 already keeps
identity header *values* out; this decision goes further and governs the eventual treatment of
identities and PII end to end. Recorded now as policy; implementation is deliberately deferred
(it would slow active development) and queued as a cross-cutting plan:
[`../../CHECKLIST-pii-tokenization.md`](../../CHECKLIST-pii-tokenization.md).

- **Tokenize principal identity.** Even the *name* of a principal may be too revealing. The
  journal carries a stable opaque **token** in place of a principal identity; the mapping from
  token → real identity lives in a **separate sidecar file**, never in the journal.
- **Tokenize likely-PII request data.** Any part of a request we can identify as a likely PII
  candidate (body fields, query values, headers) is tokenized the same way. Detection is
  *fuzzy*: pattern-based where patterns are clear (emails, GUIDs, bearer tokens, …) and manual
  annotation where they are not.
- **The identity/PII map is encrypted, opt-in to decrypt.** The token → identity sidecar is
  PII and must not be stored in plaintext. It is encrypted under a key whose use requires an
  **explicit developer choice** to decrypt. Key containment / selection / exchange is not
  designed yet; we will adopt an existing, vetted standard rather than invent one (Design
  Autonomy: we own the requirement; a standard satisfies it).
- **Downstream must not re-leak.** cartographer (and any consumer) operates on tokenized
  journals and must never emit raw identities or PII into specs or the environment descriptor
  (D-CART-4). The `full`-mode example bodies (D-AJ-3) are the largest PII vector and are in
  scope for tokenization.
- **Default capture is already PII-safe; `full` mode is the one production vector.** The
  default (`bodies: shapes`, and `none`) captures no identity *values*, header *names* only, and
  body *shapes* only — safe to run against production. `full` mode captures literal bodies,
  which can contain customer PII; it must not be run against production traffic until PII-D2
  (body scrub / tokenization) lands, and enabling it should warn.
- **Sequencing guardrail (no raw identity before PII-A).** No raw principal identity may be
  introduced into capture or the descriptor before PII-A (tokenized identity) lands. PII-A is a
  hard prerequisite of cartographer's deferred *richer inbound-caller attribution* and of
  *identity-axis role subdivision* (EM-D3); when identity is introduced it arrives tokenized.
- **Deferral is intentional.** This is *not* to be implemented during the current development
  push; it is queued so the requirement is not lost.
