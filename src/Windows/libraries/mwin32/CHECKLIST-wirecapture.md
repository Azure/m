# CHECKLIST — mwin32 wire capture (real-socket HTTP contract lifecycle demo)

Feature scope (mwin32 side): add Winsock interception to the link-time alias so an
ordinary client/server pair (separate processes, real TCP) has its HTTP traffic
**teed** (never altered — connection stays up) into a `.pilcfg`-selected capture
sink. The sink either **records** clean traffic (→ PIL recorder emits an OpenAPI
YAML spec) or **validates** live traffic against a loaded spec and tallies
contract violations in both directions (bad request: `validate_request`; bad
response: `validate_response`).

Pairs with the PIL recorder in
[`src/libraries/pil/CHECKLIST-contract-recorder.md`](../../../libraries/pil/CHECKLIST-contract-recorder.md).

Design context: mwin32 alias retargets undecorated Win32 calls into `mXxx` shims
selected per-process by a `<exe>.pilcfg` sidecar (see existing registry/filesystem
samples). Native HWC contract capture is synthetic-edge only (D-HWC-11); this
milestone adds the real-socket path the demo requires. Scoping decision for v1:
HTTP/1.1 with `Content-Length` framing only (chunked transfer-encoding is recorded
as an unsupported-framing limitation, not reassembled). Record this in DESIGN-NOTES.

## Milestone M-WIRECAP-SOCK — Winsock interception

- [ ] **WC-1**: Add Winsock shims (`msocket`, `mconnect`, `maccept`, `msend`,
  `mrecv`, `mclosesocket`, plus `mWSASend`/`mWSARecv` as needed) that forward to
  the genuine `ws2_32` entry points and tee transferred bytes per socket. Add
  exports to `mwin32.def` and the alias generation. Smoke test: passthrough is
  byte-identical (tee never mutates the stream).

- [ ] **WC-2**: HTTP/1.1 reassembler. Per-connection request-stream and
  response-stream parsers that turn the teed byte stream into complete messages:
  request `(method, path, headers, body)`, response `(status, headers, body)`,
  `Content-Length` framed. Handle partial reads and multiple messages on a
  keep-alive connection. Unit tests with canned byte streams (split reads,
  pipelined keep-alive, missing/zero body).

- [ ] **WC-3**: Capture sink seam. A diagnostics object that receives reassembled
  request/response pairs and tallies them; pure side-channel (D6 — never alters or
  blocks the bytes). Unit tests assert byte forwarding is unaffected by the sink.

### Implicit end-of-milestone steps
Clean debug+release build of m_mwin32 (zero warnings), run test_mwin32, sync.

## Milestone M-WIRECAP-CFG — pilcfg wiring + capture modes

- [ ] **WC-4**: `.pilcfg` capture schema: a `capture` section selecting
  `mode = record | validate`, the contract spec path (input for validate, output
  for record), and optional endpoint/host filter. Parser + unit tests.
  > **CROSS-COMPONENT PREREQUISITE:** PIL `M-REC` (recorder + emitter,
  > [`CHECKLIST-contract-recorder.md`](../../../libraries/pil/CHECKLIST-contract-recorder.md))
  > must land first — `record` mode emits via the PIL recorder.

- [ ] **WC-5**: Wire the sink to PIL. `record` mode feeds reassembled crossings to
  `make_http_contract_recorder` and writes the emitted YAML at process shutdown.
  `validate` mode loads the spec and runs `validate_request`/`validate_response`,
  tallying violations per direction. Unit tests with synthetic byte streams for
  both modes.

### Implicit end-of-milestone steps
Clean debug+release build of m_mwin32 (zero warnings), run test_mwin32, sync.

## Milestone M-WIRECAP-SAMPLES — real-socket sample apps

- [ ] **WC-6**: Minimal HTTP/1.1 **server** sample on raw Winsock
  (`socket`/`bind`/`listen`/`accept`/`recv`/`send`), links `mwin32_alias`. Serves
  a couple of REST endpoints. Fault switch (env/arg) emits a non-conforming
  response (server→client) without breaking the connection.

- [ ] **WC-7**: Minimal HTTP/1.1 **client** sample on raw Winsock
  (`socket`/`connect`/`send`/`recv`), links `mwin32_alias`. Drives the endpoints.
  Fault switch emits a non-conforming request (client→server).

- [ ] **WC-8**: CMake wiring for both samples (alias-linked targets, install under
  the existing sample layout), and hand-authored reference OpenAPI YAML for the
  two endpoints (used to cross-check the *derived* spec).

### Implicit end-of-milestone steps
Clean debug+release build (zero warnings), run test_mwin32, sync.

## Milestone M-WIRECAP-INTEG — end-to-end lifecycle test

- [ ] **WC-9**: Integration test, **phase 1 (derive)**: orchestrate client↔server
  over a loopback socket with clean traffic and `record` mode; assert a derived
  OpenAPI YAML is produced and loads cleanly, describing both endpoints/statuses.

- [ ] **WC-10**: Integration test, **phase 2 (detect)**: load the *derived* YAML in
  `validate` mode, run with fault injection enabled in both directions, and assert
  the violation tally records a request violation AND a response violation while
  both connections complete (traffic not broken). Records the full demo lifecycle.

### Implicit end-of-milestone steps
Clean debug+release build (zero warnings), run test_mwin32 both configs, sync.
