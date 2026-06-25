# wordy — Design Notes

`wordy` is a "shared dictionary" REST service implemented as a Rust IIS native
module. Within this repository it serves two purposes: it is the proof harness
for the link-time host-call redirection built in
[`windows-win32-shim`](../windows-win32-shim) (see that crate's SHIM-D19), and it
is where genuine Hostable Web Core (HWC) business logic is grown. This file
records `wordy`'s own design decisions; the surrounding isolation strategy lives
in the shim's design notes.

## WD-D1 — The shim-unaware contract

`wordy` is **deliberately unaware** of any isolation machinery. This is a
load-bearing property, not an accident:

- `wordy` has **no dependency** on `windows-win32-shim` and references none of its
  types, exports, or concepts.
- `wordy` contains **no isolation code** — no aliasing, no shimming, no `.pilcfg`,
  no awareness that its host calls might be redirected.
- The **only** external artifacts the isolation harness may attach to `wordy` are
  *link inputs* supplied through the environment at build time (see WD-D2). The
  decision to isolate `wordy` is therefore made entirely from the outside, exactly
  as it would be for a third-party application whose source we do not control.

A plain build with no environment variables set produces an ordinary IIS native
module; that plain build *is* the evidence that the crate carries no isolation
knowledge.

## WD-D2 — Generic env-driven `build.rs`

`build.rs` injects extra link inputs **only** when `WORDY_EXTRA_LINK_SEARCH`,
`WORDY_EXTRA_LINK_OBJ`, and/or `WORDY_EXTRA_LINK_LIB` are set, and otherwise does
nothing. These names are generic ("extra link object", "extra link lib"); the
script carries no knowledge of aliases or shims. Build-script directives are used
rather than external `RUSTFLAGS` / `.cargo/config.toml` because they are scoped to
this crate's final artifact and cache deterministically, without leaking link
flags to sibling crates.

## WD-D3 — `wordy` declares its own IIS ABI (peer of `mwinweb`)

The IIS `httpserv.h` native-module interfaces are not part of `windows-sys`, so
the ABI is hand-rolled as `#[repr(C)]` vtables. `wordy` declares its **own** copy
(`src/iis.rs`) rather than reusing the shim's `mwinweb` module — reusing
`mwinweb` would couple `wordy` to the shim and break WD-D1. The modest
duplication is intentional and acceptable; extracting a neutral
`iis-native-module` crate that both could share is deferred.

As of **MW16-1** these vtables are **pinned to the real `httpserv.h` layout**
(Windows SDK 10.0.26100), not a self-consistent modeled subset: every interface
`wordy` touches is laid out at the genuine slot offsets, so a real host's calls
land on the right methods and `wordy`'s calls invoke the right host methods. In
particular `CHttpModule` is the full 30-slot vtable (`[0]OnBeginRequest`,
`[29]Dispose`; the other 28 slots are safe pass-through stubs, since `wordy`
registers only `RQ_BEGIN_REQUEST`), and `IHttpContext` / `IHttpRequest` /
`IHttpResponse` are laid out through the slots `wordy` calls. Request decoding
uses the genuine methods (`GetRawHttpRequest` for the URL, `GetHttpMethod`,
`GetHeader`, `ReadEntityBody`); response writing uses `Clear` / `SetStatus` /
`SetHeader` / `WriteEntityChunks`. Two `http.h` structs (`HTTP_REQUEST`,
`HTTP_DATA_CHUNK`) are pinned for the data read/written, guarded by compile-time
`offset_of!` assertions. The emulated-host unit tests construct these
real-shaped vtables, so the boundary is exercised off-host; genuine activation
is MW16-2/MW16-3.

## WD-D4 — `RegisterModule` is exported under its real name

IIS loads `wordy.dll` directly and calls its `RegisterModule` export by name, so
`wordy` exports `RegisterModule` (not the shim's internal `mRegisterModule`). All
`unsafe` lives in `src/iis.rs`, which the crate root opts into explicitly while
denying `unsafe_code` everywhere else.

## WD-D5 — Safe routing core split from the ABI boundary

Request handling is split so the decision logic is testable without a host:
`src/routes.rs` is pure, safe, and platform-independent (method + URL → outcome),
while `src/iis.rs` only decodes the host request into strings and realizes the
outcome against the host response. MW13-1 seeds the dispatcher with a single
health route (`GET /healthz` → 200); later milestones grow it into the full
dictionary surface.

## WD-D6 — Word-domain core (`src/words.rs`)

The word business logic lives in `src/words.rs`, a pure, safe,
platform-independent module with **no IIS, filesystem, or isolation awareness**
(MW13-2). It is the "no-HWC end-goal in miniature": the dictionary behavior runs
and is fully unit-tested with zero host.

**Owned specification (Design Autonomy).** `wordy` defines its dictionary
behavior; `fst` and `regex` are chosen because they satisfy it:

- The shared dictionary is a **case-folded** set: every source entry is
  lowercased on load, so membership, enumeration, and suggestions are
  case-insensitive. (Distinct-case forms such as *March* / *march* collapse —
  acceptable for a spell/word service.)
- **Membership** is exact after folding; **enumeration** anchors the supplied
  `regex` to the whole word (so `cat` matches only `cat`, never `cats`); both
  enumeration and suggestions return results in ascending lexicographic order.
- The **anagram** solver is positional: an `AnagramQuery` carries a `template`
  (its length fixes the candidate length; `Slot::Fixed` positions are free
  givens that must match and do **not** draw from the tray; `Slot::Blank`
  positions must be filled from the tray), a `LetterMultiset` tray, and a count
  of **wildcard** tiles that each cover any one needed letter the tray lacks.
  Feasibility is a per-letter multiset cover plus a wildcard-bounded deficit; a
  word with any non-`a`–`z` character (e.g. a possessive) can never fill a blank
  and is rejected. The `parse` form uses `'.'` for a template blank and passes
  wildcards as a separate count so a tray of real letters is never ambiguous.
- **Suggestions** return every dictionary word within a Levenshtein edit
  distance of the folded query; an empty query yields none.

**Single `fst` store.** Word forms are held once in an `fst::Set`, which serves
membership (`contains`), ordered enumeration (stream + regex test), and
edit-distance suggestions (`Levenshtein` automaton) from one compact, sorted
structure — the reason `fst` was chosen over parallel hash/trie structures. The
set is built once per process via a `LazyLock` singleton
(`Dictionary::shared(Locale::EnUs)`); tests share it, keeping the suite well
under the unit-test time budget.

## WD-D7 — Vendored SCOWL `en-US` list + license

The shared dictionary data is `wordlist/en_US.txt` (one lowercased-on-load word
per line), generated by the SCOWL / ESDB custom-list tool (US spelling, medium
size, diacritics stripped, no special/offensive categories — so the list is pure
ASCII and excludes profanity by default). Its governing copyright and permission
notice travels beside it in `wordlist/COPYING.SCOWL` (the "+ its license file" of
MW13-2); the data is embedded into the binary with `include_str!`. A `Locale`
enum namespaces the data so additional locales drop in without a schema change;
only `EnUs` is populated today.

## WD-D8 — Custom-dictionary FS store (`src/custom.rs`)

The mutable, per-user custom dictionary (MW13-3) is a directory tree
`{root}/{locale}/{user}/` in which **each word is an empty marker file whose name
encodes the word**. Every operation is a namespace / metadata act — `create_new`
to add, `try_exists` to test, `remove_file` to delete, `read_dir` to enumerate —
and never reads or writes file content, keeping `wordy`'s mutable state inside
exactly the filesystem surface windows-win32-shim isolates (SHIM-D6 alignment).

Word ↔ filename uses an owned, reversible encoding ([`encode_token`] /
[`decode_token`]): lowercase, then percent-encode every byte outside `a`–`z`.
The encoded name contains only `[a-z]` and `%XX` escapes, so it can never be a
path separator, `.`, or `..` — hostile input (e.g. `../../escape`) is neutralized
into a single safe filename inside the user directory. The **same** encoder
sanitizes the user component, so an `X-Wordy-User` header value is always a safe
single directory name. Case is folded by the encoding, so the custom dictionary
is case-insensitive, consistent with the shared dictionary (WD-D6).

Identity is modeled forward-compatibly: a `UserId` newtype wrapped in a
`Principal`, resolved from the `X-Wordy-User` header and defaulting to a single
built-in user (`"default"`) when absent — the "app reads its own claims" posture
of a real HWC application, threaded through every store operation even though no
real logon exists yet.

## WD-D9 — REST surface, JSON models, and the body-write path (MW13-4)

The full synchronous REST surface is owned by `src/routes.rs` via a `Service`
(shared dictionary + a `CustomDictionary`) whose `dispatch(&HttpRequest) ->
Outcome` maps every route to the domain core / FS store: `GET /healthz`,
`POST /spellcheck`, `POST /anagram`, `GET /shared?pattern=`,
`GET /custom?pattern=`, and `POST` / `DELETE` / `GET /custom/{word}`. Unknown
method/path pairs return `Outcome::Continue` so the host pipeline proceeds.
Request/response bodies are modeled as `serde`/`serde_json` structs; malformed
JSON, bad regex, and invalid anagram templates become `400` JSON errors, custom
I/O failures `500`. Enumeration/anagram/suggestion result counts are capped to
bound response size. The dispatcher stays pure and host-free (WD-D5), so the
entire surface is unit-tested without a host.

The IIS boundary (`src/iis.rs`) is extended to **read** the request body and the
`X-Wordy-User` header and to **write** a response: the modeled `IHttpResponse`
gains `Clear` / `SetHeader` / a body-write alongside `SetStatus`, and the modeled
`IHttpRequest` gains header and entity-body reads. These additions are modeled
simplifications of the genuine `httpserv.h` surface (`WriteEntityChunks`,
`ReadEntityBody`, `GetHeader`), whose exact layouts are pinned when a real host
is bound (MW16). The process-wide `Service` is a `LazyLock` rooted at
`WORDY_CUSTOM_ROOT` (default: a temp-dir subdirectory). The emulated-host unit
tests are extended to supply a body + header and capture the cleared flag,
status, content type, and written body, proving a JSON route flows end-to-end
through the boundary.

## WD-D10 — Integration harness + HWC readiness pre-flight; genuine activation deferred (MW13-5 → MW16)

`tests/host.rs` drives the **entire** REST surface end-to-end through the public
`routes::Service` over a scratch custom-dictionary store at integration scale
(hundreds of add/remove/list operations, batch spell-check, anagram, regex
enumeration, per-user isolation), asserting dictionary behaviors. This is the
host-agnostic end-to-end harness; the IIS ABI boundary itself is covered by the
emulated-host unit tests in `src/iis.rs` (WD-D9).

`src/bin/wordy-host.rs` is the `wordy-host` activator/pre-flight. By default it
**discovers** the genuine `hwebcore.dll` at the absolute `inetsrv` path,
**locates** the built `wordy.dll`, **generates** a representative
applicationHost/web.config that loads it, and reports an HWC readiness verdict,
exiting `0` everywhere (safe in CI and on machines without HWC). With
`WORDY_HOST_PROBE=1` it additionally `LoadLibraryExW`s the real engine by
absolute path with the `inetsrv` dependency directory and resolves its three
exports — proving the documented dynamic-load seam (a bare-name load fails with
`ERROR_MOD_NOT_FOUND` because of the engine's `inetsrv` dependency closure) —
then frees it without activating.

**Re-plan (execution-driven).** Genuine `WebCoreActivate` + live HTTP was
separated out of MW13-5 into a new milestone **MW16**. The reason is a real
prerequisite that the original plan under-acknowledged: `wordy`'s modeled IIS
vtables (WD-D3) match only the *subset* of `httpserv.h` it uses, in a
self-consistent ordering sufficient for the emulated host — but a genuine host
calls `CHttpModule`'s full ~30-slot notification vtable, so activating against
the modeled subset would mis-dispatch. Pinning the real `httpserv.h` layout is a
substantial, SDK-dependent, crash-sensitive effort shared with MW15-2's
`hwcproof/` harness, so it is its own milestone; MW13-5 lands the runnable
integration harness + load-seam proof, and MW16 carries the genuine activation.

## WD-D11 — Genuine HWC activation, and the 500.19 config-resolution root cause (MW16)

MW16-1 pinned the real `httpserv.h` vtables (WD-D3). MW16-2 made `wordy-host`
genuinely activate Hostable Web Core. MW16-3 drove every route end-to-end over
**real HTTP** into `wordy` under the genuine host. The full path is **verified
working** on a machine with `IIS-HostableWebCore` installed:

- `WebCoreActivate` succeeds (`HRESULT 0x00000000`) against the generated
  `applicationHost.config`, and `WebCoreShutdown` cleanly stops it.
- The genuine engine loads `wordy.dll`; IIS calls its exported `RegisterModule`;
  `SetRequestNotifications(RQ_BEGIN_REQUEST)` returns `S_OK`.
- Per request, IIS instantiates the module via `IHttpModuleFactory::GetHttpModule`
  (from the request pool via the supplied `IModuleAllocator`), dispatches
  `CHttpModule::OnBeginRequest` (slot 0), and `wordy` decodes the request,
  dispatches it through `routes::Service`, writes the JSON response, and finishes
  the request — `GET /healthz` → `200 {"status":"ok"}`, `POST /spellcheck` →
  `200 {"results":[…]}`, and so on for all seven routes.

**The root cause of the earlier dispatch failure was the generated config, not
`wordy`.** A hand-rolled `applicationHost.config` that declares only a *subset*
of the standard `<configSections>` is invalid: IIS core and the loaded modules
read sections such as `system.webServer/staticContent`, `httpProtocol`, and
`serverRuntime` while resolving each request, and when a section a loaded module
reads is **undeclared**, IIS aborts the *entire request* at config resolution
with **HTTP 500.19** (`ERROR_NOT_FOUND`, `0x80070490`) — *before* the request
notification pipeline runs. That produced the exact signature observed: IIS
instantiated the module per request (`GetHttpModule`) and disposed it at request
end, but dispatched **no** `CHttpModule` notification, returning a bare `500`
(empty because no `CustomErrorModule` was loaded to render a detailed body).

The diagnosis path (preserved here because hand-rolled HWC configs are an easy
trap): per-slot trace trampolines proved no notification slot was invoked;
Failed Request Tracing wrote nothing and the Windows event log was empty (both
consistent with a *pre-pipeline* abort); enabling W3C site logging surfaced
`sc-status 500`, `sc-substatus 19`, `sc-win32-status 1168`; and loading
`custerr.dll` with `errorMode="Detailed"` made IIS name the offending section in
the response body. Declaring the **complete** standard section set resolved it in
one shot.

The generator (`wordy-host::application_host_config`) now emits the full standard
`<configSections>` plus the core `inetsrv` pipeline modules (protocol support,
anonymous auth, request filtering, custom errors, static file) and `WordyModule`.

**Tooling** on the bin: `WORDY_HOST_ACTIVATE=1` (activate), `WORDY_HOST_HTTP=1`
(drive localhost routes), `WORDY_HOST_DUMP=1` (dump raw responses),
`WORDY_HOST_CONFIG=<path>` (use an external `applicationHost.config`), and
`WORDY_HOST_FREB=1` (register the genuine `iisfreb.dll` Failed Request Tracing
module). `iis.rs` carries a `WORDY_TRACE=<file>` gated trace through the boundary
and per-slot `notify_slot::<N>` trampolines that self-identify any unexpected
notification a future host might dispatch. The live-HTTP path is covered by the
`hwc_genuine_http_dispatch_end_to_end` integration test, which exercises genuine
HWC **by default** on a capable host; `WORDY_HWC_EMULATED_ONLY=1` opts out, and it
skips its assertions when HWC is absent or the listener cannot bind without
elevation.
