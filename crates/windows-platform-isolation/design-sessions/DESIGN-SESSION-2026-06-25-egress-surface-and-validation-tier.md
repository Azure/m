# DESIGN SESSION — Egress (network-client) isolation surface + validation tier

**Date:** 2026-06-25
**Resulting decisions:** platform-isolation **D31** (egress surface); windows-win32-shim
**SHIM-D22** (WinHTTP egress seam), **SHIM-D23** (validation tier: dictionary-store
service + `wordy` split + native async file I/O).
**Realizes:** the long-standing **D27** "coordinated multi-surface isolation
(network + filesystem + registry)" — network was always "the majority of the
surface" (D27) and an explicitly named alias target (D24: "the HTTP-client / RPC
entry points"); it simply had not been built yet.

---

## 1. Why now — the WireServer egress finding

Isolation work so far interposes on a process's *local* host calls — filesystem,
registry, loader, COM, and the IIS request/response vtables. Reading the real
**WireServer** (`Q:\src\Compute-Fabric-HostAgent\src\agent\WireServer`) showed
that the defining behavior of a relay service is **egress**, and it uses two
distinct outbound client stacks:

- **WinHTTP** (`winhttp.dll`) — the REST forwarders. `HostNetAgentQueryForwarder`
  (`localhost:8019`), `InstanceMetadataFromServerHandler` (IMDS),
  `AzureStackRequestHandlerBase`, `NetAnalyticsNodeAgentQueryHandler` all run the
  textbook sequence `WinHttpOpen → WinHttpConnect → WinHttpOpenRequest →
  WinHttpAddRequestHeaders → WinHttpSendRequest → WinHttpReceiveResponse →
  WinHttpQueryHeaders → WinHttpQueryDataAvailable → WinHttpReadData →
  WinHttpCloseHandle`.
- **WWSAPI** (Windows Web Services API, `webservices.dll`) — the typed SOAP
  control-plane to RdAgent / FC Agent. `AgentClient` builds a `WS_SERVICE_PROXY`
  over `WS_HTTP_CHANNEL_BINDING` and dispatches `wsutil`-generated stubs
  (`RdDefaultBinding_IWireAgent_QueryGoalState`, `…_ReportHealth`, …).

Inbound is an IIS native module (`modrest.dll`: `RegisterModule` →
`CHttpModule::OnBeginRequest(IHttpContext*)`) self-hosted in **Hostable Web Core**
(`WebServer.cpp` → `WebCoreActivate`) — the exact HWC/native-module shape the shim
already targets.

**Key link-time nuance.** IAT aliasing only redirects what *the app itself*
imports. WWSAPI calls WinHTTP *inside* `webservices.dll`, so aliasing `winhttp.dll`
in the app does **not** catch the RdAgent SOAP egress. Each stack must be
intercepted at the app's own import boundary: WinHTTP for the direct REST
forwarders, WWSAPI (`Ws*`) for `AgentClient`. They are independent seams.

---

## 2. Scope (owner's directive, 2026-06-25)

> "I would like the isolation layers in place so that we can work against a
> **memory buffered**, and a **redirected** (URL / IP / port changed) service,
> with various **'system' state pre-loaded**. I do **not** expect the goal to be
> to learn and emulate an arbitrary web service."

So the egress surface is the **mode stack of D25** plus a redirect mode — **not** a
learning proxy / MITM emulator:

| Mode | What it does | Maps to |
|---|---|---|
| **passthrough** | forward each call to the real client unchanged (true identity) | D25 off |
| **redirect** | rewrite destination (scheme/host/port/path) by rule, then send for real | new |
| **buffer** | capture mutating requests in memory, return a synthetic ack; the live destination is never contacted | D25 record-without-forward (peer of `FsBuffered`/registry `Buffered`) |
| **replay** | serve canned responses from preloaded fixtures (the "system state pre-loaded"); miss → block or passthrough | D25 replay / D15 ingress |
| **block** | deny with a synthetic error | negative-path testing |
| **observe** | record `(method, host, path, status)` to the sink, then forward | D29 + D28 (PII-first) |

**Deferrals (recorded so they are intentional, not gaps):**
- **WWSAPI / SOAP egress is deferred.** The first pass is **WinHTTP only** — it
  covers the REST forwarders, which are the bulk of the relay, and the validation
  tier (below) is REST. WWSAPI is a later seam at the `Ws*` boundary.
- **No payload learning / smart emulation.** Replay is fixture-driven; we do not
  synthesize responses we were not given.
- **Coordinated cross-surface journaling (D27 "one recorded world")** is the
  long-term shape; the first pass keeps egress as its own surface and leaves the
  shared-journal coupling to a later milestone.

---

## 3. Architecture

```
unmodified app  --__imp_WinHttp*-->  m WinHttp* front-end (shim)
                                         |  reassembles HINTERNET lifecycle
                                         |  into one EgressRequest at SendRequest,
                                         |  drains the chosen EgressResponse back
                                         v
                                   EgressBacking (session, from .pilcfg)
                                         |
              passthrough / redirect / buffer / replay / block / observe
                                         |
                                   EgressSurface (platform-isolation)
                                         |
                                   LiveEgress (real WinHTTP, when a send is needed)
```

### 3.1 platform-isolation — the surface (M11, D31)

A new surface mirroring `FsSurface` / `Surface`:

```rust
pub enum EgressTransport { Http /* , Soap (reserved) */ }
pub struct EgressRequest {
    pub transport: EgressTransport,
    pub scheme: Scheme, pub host: Utf16, pub port: u16,
    pub verb: Utf16, pub path: Utf16,
    pub headers: Vec<(Utf16, Utf16)>,
    pub body: Vec<u8>,
}
pub struct EgressResponse { pub status: u32, pub headers: Vec<(Utf16, Utf16)>, pub body: Vec<u8> }

pub trait EgressSurface { fn send(&mut self, req: &EgressRequest) -> EgressResult<EgressResponse>; }
```

Decorators (the D4 stack): `RedirectingEgress<S>` (rule rewrite → inner),
`BufferedEgress<S>` (capture mutations, optional read-through to inner),
`ReplayEgress<S>` (fixtures, miss → inner/block), `BlockingEgress`,
`ObservingEgress<S>`. The network bottom is `LiveEgress` (cfg windows: a real
WinHTTP transaction from one `EgressRequest`). All decorators are pure safe-half
and unit-testable over an in-memory inner; `LiveEgress` is the only `unsafe`-leaf
consumer (its own `-sys` crate, per D1/D13).

**Transaction reassembly is the shim's job, not the surface's.** The surface sees
whole request/response values; the shim translates the multi-call HINTERNET
lifecycle into them. When no egress mode is configured the shim does a transparent
1:1 passthrough and never builds a surface at all (so passthrough is a perfect
identity, D25).

### 3.2 windows-win32-shim — the WinHTTP seam (MW17, SHIM-D22)

- `.pilcfg` gains an `egress` section (`mode`, `redirections:[{from,to}]`,
  `replay_dir`), strict-parse / tolerant-load like SHIM-D5.
- An `HINTERNET` handle table + per-handle transaction state accumulates
  scheme/host/port/verb/path/headers/body across `Open/Connect/OpenRequest/
  AddRequestHeaders`, captures at `SendRequest`, and drains the response across
  `ReceiveResponse/QueryHeaders/QueryDataAvailable/ReadData/CloseHandle`. This is
  the same replay-state-in-a-handle shape as `FindFirstFile`/`FindNext` (SHIM-D14).
- An `EgressBacking` enum in the session (Passthrough/Buffered/Redirecting/Replay/
  Blocking) selected from `.pilcfg`, consuming platform-isolation M11.
- NDJSON alias entries for the WinHTTP exports actually used; `m`-prefixed exports;
  `.def`↔`.ndjson` parity; `dumpbin /imports` verification.
- `egressproof/` harness (mirrors `linkproof/`): a synthetic app doing a real
  `WinHttpSendRequest`, with a `.pilcfg` redirect to a localhost echo and a buffer
  mode that contacts nothing; assert interception + a non-aliased negative control.

### 3.3 The validation tier (MW18, SHIM-D23)

Split `wordy` so its custom-dictionary state lives in a **separate web service**,
making `wordy`'s calls to that service the egress we isolate:

- **New crate `windows-file-io`** — native async Win32 file I/O: overlapped
  `CreateFile`/`ReadFile`/`WriteFile` with completion delivered via the Windows
  thread pool (`CreateThreadpoolIo` / `StartThreadpoolIo`, built on
  `windows-threadpool`'s IOCP reactor, M7-3 / TP). The API is written **async /
  completion-based even though small ops often complete synchronously** — the
  synchronous-completion fast path is handled, but the code is shaped as if it does
  not (owner's directive). Its own `-sys` leaf for the `unsafe`.
- **New crate `wordstore`** — a REST dictionary-store service owning the custom
  dictionary on disk (add / update / store / remove / enumerate) via
  `windows-file-io`. Inbound via the **HTTP Server API (http.sys)** — a second,
  simpler inbound stack than HWC, async-friendly, no IIS config — with a pure
  request-dispatch core testable off the listener (mirroring `wordy::routes`).
- **Gut `wordy`** — remove the local filesystem custom store; the custom-dict ops
  (add/remove/contains/list) become a **WinHTTP client** relaying to `wordstore`.
  `wordy` keeps the shared-dictionary spell-check / match / anagram / `fst` work.
  `wordy` stays shim-unaware (SHIM-D19): it just makes ordinary WinHTTP calls.
- **End-to-end isolation proof**: run aliased `wordy` + `wordstore` and prove the
  three requested modes against a real service — **redirect** (`wordy`'s
  `wordstore` URL rewritten to a second instance), **buffer** (dict mutations
  captured, `wordstore` untouched), **replay** (dict reads served from egress
  fixtures with `wordstore` offline).

---

## 4. Decisions made this session

- **D-EGRESS-1 (→ D31).** Egress is a first-class surface realizing D27; modes are
  the D25 stack plus a `redirect` mode; reassembly of the WinHTTP handle lifecycle
  is the shim's responsibility; passthrough is a true link-time identity.
- **D-EGRESS-2 (→ SHIM-D22).** First seam is **WinHTTP only**; WWSAPI/SOAP is a
  deferred peer seam at the `Ws*` import boundary (it cannot be reached by aliasing
  `winhttp.dll` because WWSAPI's WinHTTP calls are internal to `webservices.dll`).
- **D-EGRESS-3 (→ SHIM-D23).** The validation tier splits `wordy`: a new
  `wordstore` REST service owns the on-disk dictionary; `wordy` relays to it over
  WinHTTP. This makes the egress seam testable against a *real* dependent service,
  not a synthetic stub.
- **D-EGRESS-4 (→ SHIM-D23).** `wordstore` disk I/O uses **native async Win32**
  (overlapped + thread-pool I/O completion) in a reusable `windows-file-io` crate,
  written async-first even though completions are usually synchronous.
- **D-EGRESS-5 (→ SHIM-D23).** `wordstore` inbound uses the **HTTP Server API
  (http.sys)** with a listener-independent dispatch core, chosen over HWC to avoid
  a third IIS-native-module duplication and to keep its tests light.

## 5. Open questions / flags for the owner

- **`windows-file-io` vs. extending `windows-threadpool-executor`.** A dedicated
  file-I/O crate is proposed; if the executor should own this instead, say so.
- **http.sys urlacl.** `wordstore`'s listener needs a URL reservation (same
  constraint as HWC); its core is tested without the listener, and the listener
  edge is a gated integration test (reuses the urlacl preflight tooling).
- **Naming.** `wordstore` / `windows-file-io` are placeholders; rename freely.
- **WWSAPI timing.** Deferred for now; revisit once the WinHTTP seam + validation
  tier land, or sooner if a SOAP-egress consumer becomes the priority.
