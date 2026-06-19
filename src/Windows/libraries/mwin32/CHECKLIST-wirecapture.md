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

Transport-topology matrix (D19): the capture core is **transport-agnostic** — the
tee takes raw bytes off `send`/`recv`, the reassembler turns bytes into HTTP
messages, the sink records/validates; none of them learn the address family or the
process model. The demo is therefore exercised across a matrix of *address paths*
(arbitrary DNS name → `getaddrinfo` → connect; IPv4 loopback `127.0.0.1`; IPv6
loopback `::1`) and *process models* (in-process two-thread loopback socket for
deterministic CI; the in-process synthetic edge with no Winsock at all; optionally
two separate processes for the hand-run demo). Optional further transports: AF_UNIX
(IP-less). Layering rule: **topology lives in the sample apps and the test
harness, never in the capture `.pilcfg` schema** — the server chooses its bind
family and the client chooses its connect target by CLI arg; the capture pipeline
stays family-blind (preserves D6 pure side-channel). Proving the derived spec and
the violation tallies are equal across every transport is the headline result.

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
  a couple of REST endpoints. **Bind-family parameter** (`--family ipv4|ipv6|dual`)
  and port selection (fixed, or `0` for an ephemeral port echoed to stdout so a
  harness can read it back). Fault switch (env/arg) emits a non-conforming
  response (server→client) without breaking the connection.

- [ ] **WC-7**: Minimal HTTP/1.1 **client** sample on raw Winsock
  (`socket`/`connect`/`send`/`recv`), links `mwin32_alias`. Drives the endpoints.
  **Target selector** (`--target dns:<host>:<port>` → `getaddrinfo`; `ipv4:<port>`
  → `127.0.0.1`; `ipv6:<port>` → `::1`). Fault switch emits a non-conforming
  request (client→server).

- [ ] **WC-8**: CMake wiring for both samples (alias-linked targets, install under
  the existing sample layout), and hand-authored reference OpenAPI YAML for the
  two endpoints (used to cross-check the *derived* spec).

### Implicit end-of-milestone steps
Clean debug+release build (zero warnings), run test_mwin32, sync.

## Milestone M-WIRECAP-INTEG — end-to-end lifecycle test (topology matrix)

- [ ] **WC-9**: Reusable **in-process harness** (server + client on two threads in
  the test process over a real loopback socket; bind port `0` and read back the
  ephemeral port; topology selector for IPv4 / IPv6 / DNS / synthetic). On top of
  it, **phase 1 (derive)** over IPv4 loopback: clean traffic + `record` mode
  asserts a derived OpenAPI YAML is produced and loads cleanly, describing both
  endpoints/statuses.

- [ ] **WC-10**: Integration test, **phase 2 (detect)** over IPv4 loopback: load
  the *derived* YAML in `validate` mode, run with fault injection in both
  directions, and assert the violation tally records a request violation AND a
  response violation while both connections complete (traffic not broken).

- [ ] **WC-11**: **Transport matrix** — re-run the derive→detect lifecycle over
  IPv6 loopback (`::1`), a DNS-resolved target (`getaddrinfo` of a loopback name),
  and the in-process synthetic edge (no Winsock). Assert the derived spec and the
  request/response violation tallies are **equivalent across all transports**
  (the transport-independence result). AF_UNIX and a two-separate-process smoke
  are optional extras called out here, not gating items.

### Implicit end-of-milestone steps
Clean debug+release build (zero warnings), run test_mwin32 both configs, sync.
