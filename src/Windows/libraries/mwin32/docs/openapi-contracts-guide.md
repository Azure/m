# Using the OpenAPI capture, verification, and testing features of `mwin32.dll`

`mwin32.dll` can observe, verify, and synthesize HTTP traffic against an
OpenAPI/Swagger contract **without changing a single line of the host
application**. Everything is driven by a sidecar JSON file named
`<executable>.pilcfg` placed next to the host `.exe`. This guide explains the
two independent features, the `.pilcfg` members that drive them, and the
end-to-end workflows.

> All behavior described here is **specified and owned by mwin32**; the OpenAPI
> engine underneath is an implementation detail. A missing or malformed
> `.pilcfg` always degrades to plain passthrough — it never breaks the host.

---

## 1. Two features, two schemas

There are two separate OpenAPI capabilities. They use **different** `.pilcfg`
blocks and apply to different kinds of host. You can use either one alone.

| Feature | `.pilcfg` block | Applies to | What it does |
|---|---|---|---|
| **Wire capture** | top-level `capture` | any app that talks HTTP/1.1 over raw Winsock (`send`/`recv`/`WSASend`/`WSARecv`) | Tees the socket bytes, reassembles HTTP messages, and either **derives** a spec from the traffic or **validates** the traffic against one. |
| **Webcore contracts** | `webcore.contracts` | apps hosting the in-process web engine (Hostable Web Core) through the `mWebCore*` surface | Binds an OpenAPI spec to a webcore endpoint and either **validates** the live edge traffic or **drives** synthesized traffic from the spec. |

`capture` uses `mode = "record" | "validate"`.
`webcore.contracts` uses `mode = "validate" | "drive"`.
The two `mode` vocabularies are deliberately distinct — `"drive"` is rejected by
the capture parser, and `"record"` is rejected by the contract parser — so the
features can never be confused.

---

## 2. Prerequisites — engaging the shim

The OpenAPI features only run when `mwin32.dll`'s shims are actually in the call
path. Engage them the same way as every other mwin32 capability:

1. **Link the alias object.** Add `mwin32_alias` to the host's link line. This
   redirects the genuine Win32 entry points (the Winsock `send`/`recv`/… for
   wire capture, the `mWebCore*` surface for webcore) to the mwin32 shim **at
   link time** — no source edits, no runtime patching. See
   [`mwin32-sdk-guide.md`](mwin32-sdk-guide.md) for the link details.
2. **Ship `m_mwin32.dll` next to the host `.exe`** so it loads at runtime.
3. **Place `<executable>.pilcfg`** next to the host `.exe`. For example, a host
   called `myserver.exe` reads `myserver.exe.pilcfg`.

If any of these is missing, the host runs exactly as it would without mwin32
(passthrough); nothing crashes.

---

## 3. The `.pilcfg` sidecar mechanics

`.pilcfg` is a JSON object. The OpenAPI features add two optional members on top
of the existing registry/filesystem ones:

- a top-level `"capture"` object, and
- a `"contracts"` array inside the `"webcore"` object.

Two rules govern parsing:

- **Strict parse.** Invalid JSON, a wrong member type, a missing required field,
  or an invalid `mode` is an error.
- **Tolerant load.** At runtime the loader swallows any such error and falls
  back to passthrough, so a broken sidecar can never take the host down. (Use the
  unit-testable strict parser when you want to catch mistakes early.)

### `%VAR%` expansion in paths

Every member that names a **host filesystem path** (for example a `spec` path)
is expanded against the process environment at load time: a `%NAME%` token is
replaced by environment variable `NAME`; an undefined token is left verbatim; a
value with no `%` is unchanged. This lets you check in a portable config:

```json
{ "capture": { "mode": "record", "spec": "%TEMP%\\derived-api.yaml" } }
```

Logical identifiers (the webcore `endpoint` key, redirection keys) are taken
**literally** and are never expanded.

---

## 4. Feature A — Wire capture (`capture`)

Use this for any application that speaks HTTP/1.1 over raw sockets. mwin32's
Winsock shims copy the transferred bytes off each socket, reassemble them into
request/response messages (HTTP/1.1, `Content-Length` framing), pair them, and
hand each crossing to a capture sink. The capture **never alters the wire** — the
bytes on the genuine socket are byte-identical with or without it.

### 4.1 Schema

```json
{
  "capture": {
    "mode": "record",
    "spec": "%TEMP%\\derived-api.yaml",
    "host": "api.contoso.com"
  }
}
```

| Member | Required | Type | Meaning |
|---|---|---|---|
| `mode` | yes | string | `"record"` (derive a spec) or `"validate"` (check traffic against a spec). Any other value is an error. |
| `spec` | yes | string (non-empty) | The contract file. It is the **output** in `record` mode and the **input** in `validate` mode. `%VAR%`-expanded. |
| `host` | no | string | Host-header filter. When set, only crossings whose request `Host:` header matches (ASCII case-insensitive) are captured. Absent/empty captures every crossing. Taken literally (not `%VAR%`-expanded). |

Absent `capture` ⇒ wire capture is off (the shims still tee bytes, but nothing
consumes them).

### 4.2 Record workflow — derive a spec from live traffic

Goal: run the app, let it make/serve real HTTP calls, and emit an OpenAPI YAML
spec describing what was observed.

1. Write a `record` config:
   ```json
   { "capture": { "mode": "record", "spec": "%TEMP%\\derived-api.yaml" } }
   ```
2. Run the host normally. Each observed request/response crossing is fed to the
   PIL contract recorder.
3. On process shutdown, the derived OpenAPI spec is written to `spec`.

Notes:
- The observed request **path** is used as the operation path template, with the
  query string stripped. So `/search?q=a` and `/search?q=b` collapse into one
  `/search` operation.
- Only fully paired request+response crossings are recorded; a half-open
  connection (request with no response) is simply never recorded.

### 4.3 Validate workflow — verify traffic against a spec

Goal: run the app against an existing contract and count any non-conforming
requests or responses.

1. Point `spec` at the contract you want to enforce and switch the mode:
   ```json
   { "capture": { "mode": "validate", "spec": "%TEMP%\\derived-api.yaml" } }
   ```
2. Run the host. Each crossing is checked with `validate_request` /
   `validate_response`.
3. Violations are tallied **per direction** (request vs response). A clean check
   is a conforming crossing; an operational error (e.g. a malformed spec)
   reported through the error channel is neither a conforming check nor a
   violation.

Because the only edit between 4.2 and 4.3 is one word (`record` → `validate`),
the **derive → detect round-trip** is trivial: record once to produce the spec,
then validate future runs against it.

### 4.4 Transport independence

The tee, the reassembler, and the sink never learn the socket address family,
the resolved peer, or whether the peer is in another process. IPv4, IPv6, a
DNS-resolved host, and the in-process synthetic edge all produce identical
captures. Topology therefore never appears in the `capture` schema — it is a
property of how the app connects, selected by the app itself, not by `.pilcfg`.

### 4.5 Reference samples

The SDK ships a matched pair that exercises the full lifecycle:

| Sample | Role |
|---|---|
| `mwin32_http_server_sample.cpp` | Raw-Winsock HTTP/1.1 server. `--family ipv4\|ipv6\|dual`, `--port` (`0` = ephemeral), and a fault switch to emit a non-conforming response. |
| `mwin32_http_client_sample.cpp` | Raw-Winsock HTTP/1.1 client driving the server. `--target dns:<host>:<port> \| ipv4:<port> \| ipv6:<port>`, and a fault switch to send a non-conforming request. |

Run the server under a `record` config to derive the spec, then run client and
server under a `validate` config and flip a fault switch to see the violation
tally rise in exactly one direction while the traffic stays intact.

---

## 5. Feature B — Webcore contracts (`webcore.contracts`)

Use this when the host runs the in-process web engine (Hostable Web Core)
through mwin32's `mWebCore*` surface. A contract binding attaches an OpenAPI spec
to a named webcore endpoint and is wired onto the **running** engine's edge.

### 5.1 Schema

```json
{
  "webcore": {
    "contracts": [
      {
        "spec": "%CONFIG%\\orders-api.yaml",
        "endpoint": "orders",
        "mode": "validate"
      },
      {
        "spec": "%CONFIG%\\orders-api.yaml",
        "endpoint": "orders",
        "mode": "drive"
      }
    ]
  }
}
```

| Member | Required | Type | Meaning |
|---|---|---|---|
| `spec` | yes | string (non-empty) | Host path to the OpenAPI/Swagger spec. `%VAR%`-expanded. |
| `endpoint` | yes | string (non-empty) | The logical webcore endpoint key the contract binds to. Taken literally. |
| `mode` | yes | string | `"validate"` or `"drive"`. Any other value is an error. |

`webcore.contracts` is an array; order is preserved. Absent ⇒ no contracts are
bound. (The surrounding `webcore` object also carries `interception`,
`endpoints`, and `materialization_dir`, which control how the engine is reached;
those are orthogonal to contracts.)

### 5.2 `validate` vs `drive`

- **`validate`** — the spec is registered as a crossing observer on the live
  edge. Every autonomous request/response that crosses the engine is contract
  checked and tallied. This is a pure side diagnostic: the engine's behavior is
  never altered, and validation never feeds back into the request path.
- **`drive`** — the spec's example traffic is synthesized and submitted through
  the engine's public submit seam, and the captured responses are validated.
  Drive composes on top of validate.

You can bind both modes to the same endpoint (as above): `validate` watches
real traffic while `drive` actively generates spec-derived traffic.

### 5.3 How it wires onto the engine

When contracts are present, the webcore surface returned to the host is wrapped
in a contract-wiring decorator. On `activate`, it forwards to the underlying
webcore and then, on the activated instance's synthetic HTTP edge, registers the
validate-mode observers and runs the drive-mode documents. When no contracts are
bound, the decorator is a transparent pass-through; when the activated engine
exposes no synthetic edge (e.g. a null engine), wiring is a tolerant no-op.

The wiring is identical whether the engine behind the edge is the production
in-process IIS engine (`hwebcore.dll`) or an in-process test engine — only the
engine differs, not the contract machinery.

---

## 6. End-to-end example (wire capture round-trip)

`myserver.exe.pilcfg` — first pass, derive the contract:

```json
{
  "capture": {
    "mode": "record",
    "spec": "%TEMP%\\myserver-api.yaml"
  }
}
```

Run `myserver.exe`, exercise it with real clients, stop it. `%TEMP%\myserver-api.yaml`
now contains the derived OpenAPI spec.

Second pass, enforce the contract — change one word:

```json
{
  "capture": {
    "mode": "validate",
    "spec": "%TEMP%\\myserver-api.yaml",
    "host": "myserver.local"
  }
}
```

Run again. Only traffic for `Host: myserver.local` is checked, and any request-
or response-direction violation is tallied without disturbing the live traffic.

---

## 7. Behavioral guarantees

- **Never alters the wire.** Capture reads only an observational copy of the
  socket bytes; attaching it leaves the genuine socket byte-identical.
- **Never alters the engine.** `validate`-mode webcore contracts are a side
  diagnostic only.
- **Tolerant at runtime.** A missing, unreadable, or malformed `.pilcfg`, or a
  missing/malformed spec, degrades to passthrough; the host never crashes.
- **Strict when parsed directly.** The strict parser surfaces schema mistakes
  (bad JSON, wrong types, missing required fields, invalid `mode`) so you can
  catch them in tests before shipping a config.

---

## 8. Troubleshooting

| Symptom | Likely cause |
|---|---|
| No spec produced / no violations counted | The shim is not in the call path — confirm `mwin32_alias` is linked and `m_mwin32.dll` sits next to the `.exe`. |
| `.pilcfg` seems ignored | It must be named `<executable>.exe.pilcfg` and sit next to the host `.exe`; a malformed file silently degrades to passthrough. |
| Nothing captured for some requests | A `host` filter is set and those requests' `Host:` header doesn't match. |
| `mode` rejected | `capture` accepts only `record`/`validate`; `webcore.contracts` accepts only `validate`/`drive`. The vocabularies are not interchangeable. |
| `spec` path wrong on another machine | Use `%VAR%` tokens (e.g. `%TEMP%`, `%CONFIG%`); only host-path members are expanded, not endpoint keys. |

---

## 9. See also

- [`mwin32-sdk-guide.md`](mwin32-sdk-guide.md) — linking the alias, shipping the
  DLL, and the full sample list.
- [`../COMPONENT.md`](../COMPONENT.md) — component overview and the `.pilcfg`
  mode model.
- [`../DESIGN-NOTES.md`](../DESIGN-NOTES.md) — D-HWC-8 (webcore contract
  binding), D18 (live-edge wiring), D19–D30 (wire capture).
