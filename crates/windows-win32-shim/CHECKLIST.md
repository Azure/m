# windows-win32-shim — CHECKLIST

Action-only checklist. Completed groups move to `COMPLETED-CHECKLIST.md`.
Decision references point at `DESIGN-NOTES.md` (`SHIM-D` numbers); the C++
`mwin32` DESIGN-NOTES (D1–D11) and its `test/` suite are the ABI behavioral spec.

Parallel all-Rust reimplementation of the C++ `mwin32` DLL, routing a Win32-shaped
C ABI through `windows-platform-isolation`. Scope: **filesystem and registry**,
plus the **dynamic-loader** (MW9) and **COM activation** (MW10) surfaces that
realize platform-isolation D26/D29 (no C++ `mwin32` antecedent).
Milestones are dependency-ordered, sized to ~5 items, and end in an integration
test. Sub-steps use decimal notation.

---

## MW1 — Foundation (scaffold, ABI posture, handle table, error mapping, session)

- [x] **MW1-1** Scaffold `crates/windows-win32-shim`: `Cargo.toml`
      (`crate-type = ["cdylib", "rlib"]`, edition 2024, MSRV inherited),
      `#![deny(unsafe_code)]` at the root with `#[allow(unsafe_code)]` only on the
      ABI-boundary modules (SHIM-D2), README + `DESIGN-NOTES.md` + `COMPONENT.md`.
      Dependencies (cfg(windows)): `windows-platform-isolation` (path),
      `windows-text` (path), `windows-sys` 0.59 (Win32 types/errors:
      `Win32_Foundation`, `Win32_System_Registry`, `Win32_Storage_FileSystem`).
      Add the crate to the workspace `members` list.
- [x] **MW1-2** Win32 error-mapping module (SHIM-D7): `registry_error_to_lstatus`
      and `filesystem_error_to_win32` translating `RegistryError` /
      `FilesystemError` into `LSTATUS` / Win32 codes, plus a `set_last_error`
      helper. `Os(u32)` passes through; structured variants map to documented
      codes. Owned mapping (Design Autonomy), with a unit table.
- [x] **MW1-3** Handle table (SHIM-D3 / mwin32 D11): mint `HANDLE`/`HKEY` with the
      reserved bit pattern; `intern` / `deref` / `close` behind a `Mutex` over a
      payload variant (isolation registry-key handle, file-handle state,
      find-enumeration state). Predefined `HKEY` values resolve (cached) to
      `windows-platform-isolation` well-known roots.
- [x] **MW1-4** Process-wide `Session`: lazily-initialized isolation stack holder
      vending the registry (and later filesystem) facade; default **live
      passthrough** for registry (SHIM-D8). Programmatic config only at this stage
      (`.pilcfg` is MW4). `current_exe()` is read via safe `std::env`.
- [x] **MW1-5** *(integration)* Tests: handle round-trip + reserved-bit
      invariants (minted handles never collide with predefined `HKEY`s or low-bit
      OS values), predefined `HKEY` → root resolution, and the error-mapping table.

## MW2 — Registry C ABI (W forms)

- [x] **MW2-1** `mRegOpenKeyExW` / `mRegCreateKeyExW` / `mRegCloseKey`
      (predefined handles are close no-ops), routing through the session's
      registry facade and minting result `HKEY`s.
- [x] **MW2-2** Value ops: `mRegSetValueExW`, `mRegQueryValueExW` (the Win32
      three-case size/type contract: query, `ERROR_MORE_DATA`, success),
      `mRegDeleteValueW`, `mRegGetValueW`. Value-type bytes map to/from
      `ValueData` (all six types).
- [x] **MW2-3** Enumeration / info: `mRegEnumKeyExW`, `mRegEnumValueW`,
      `mRegQueryInfoKeyW`, in `windows-platform-isolation` ordinal order.
- [x] **MW2-4** `mRegDeleteKeyExW` (subtree), plus `ERROR_NOT_SUPPORTED` stubs for
      the in-`.def` but unimplemented entries (transacted create, predefined-cache
      control, `mRegOverridePredefKey`, etc.), matching the C++ stub behavior.
- [x] **MW2-5** *(integration)* Registry tests mirroring the C++
      `test_mwinreg_predefined` / `test_mwinreg_open_close` /
      `test_mwinreg_value_ops` against a buffered in-memory stack, asserting the
      `LSTATUS` contracts and the `mRegQueryValueExW` three-case behavior.

## MW3 — Filesystem C ABI (W forms; metadata / dir / enum; content deferred)

> **⬅ CROSS-COMPONENT PREREQUISITE:** filesystem passthrough requires the live FS
> provider in `windows-platform-isolation` → **M9** (`LiveFilesystem`). See
> [`../windows-platform-isolation/CHECKLIST.md`](../windows-platform-isolation/CHECKLIST.md).
> Until M9 lands, MW3 is exercised against in-memory / artifact stacks only.

- [x] **MW3-1** `mCreateFileW` (creation disposition → create/open metadata node;
      mint a file `HANDLE` carrying handle state) + `mCloseHandle` (exported
      `noalias` / opt-in per SHIM-D4; predefined / unknown handles handled per the
      Win32 contract).
- [x] **MW3-2** Path metadata: `mGetFileAttributesW` / `mGetFileAttributesExW` /
      `mSetFileAttributesW` / `mDeleteFileW`, translating `FileMetadata` ↔ the
      Win32 attribute/`WIN32_FILE_ATTRIBUTE_DATA` shapes.
- [x] **MW3-3** Directory + handle-state ops: `mCreateDirectoryW` /
      `mRemoveDirectoryW`, and `mGetFileSizeEx` / `mSetFilePointerEx` over the
      file handle state.
- [x] **MW3-4** Directory enumeration: `mFindFirstFileW` / `mFindNextFileW` /
      `mFindClose` (find-enumeration state from `read_dir`, ordinal-ordered).
      Content + move/copy exports (`mReadFile`, `mWriteFile`,
      `mReadFileScatter`/`mWriteFileGather`, `mMoveFileExW`, `mCopyFileExW`)
      return the Win32 not-supported failure shape (SHIM-D6; `mMoveFileExW`
      additionally awaits a future isolation rename op).
- [x] **MW3-5** *(integration)* Filesystem tests mirroring the C++
      `test_mwinfile_handle_meta` / `test_mwinfile_legacy` against the live FS
      provider over a scratch temp dir (RAII cleanup) and an in-memory artifact
      stack; assert attribute/size results and ordinal `FindFirst`/`FindNext`
      ordering.

## MW4 — `.pilcfg` config (JSON sidecar; artifact parity, SHIM-D5)

- [x] **MW4-1** Choose the JSON parser dependency; model the `.pilcfg` schema
      (`buffer_updates`, `record_modifications`, `redirections`,
      `persisted_state`, `capture_snapshot`, `diagnostic_log`, `fault_script`;
      `webcore` ignored), with `%TEMP%`-style expansion as the C++ does.
- [x] **MW4-2** Tolerant sidecar load: resolve `<current_exe>.pilcfg`
      (`std::env::current_exe`, safe); absent / unreadable / malformed →
      passthrough, never failing the host (mwin32 D5).
- [x] **MW4-3** Wire config → isolation stack composition: `buffer_updates` →
      `Buffered` layer; `persisted_state` → load the `<Platform>` artifact via the
      isolation loaders; `redirections` / `record_modifications` / `fault_script`
      honored where the isolation crate supports them, documented as gaps
      otherwise.
- [x] **MW4-4** *(integration)* `.pilcfg` parity tests (mirror C++ `test_pilcfg`
      + buffered `test_mwinreg_value_ops`): a buffered fixture isolates writes
      from the live registry; `capture_snapshot` writes state on teardown.

## MW7 — End-to-end / C++ artifact parity — OUTLINE (detail when scheduled)

- [ ] **MW7-1** Load a C++-produced `.pilcfg` + `persisted_state` artifact and
      assert the Rust shim reproduces the C++ shim's observable behavior.
- [ ] **MW7-2** Packaging / SDK considerations (or record as out of scope).
- [ ] **MW7-3** *(integration)* Full end-to-end scenario: registry + filesystem
      through the shim under a single `.pilcfg`.

## MW9 — Dynamic-loader shims (`mLoadLibrary*` / `mGetProcAddress` / module handles, SHIM-D16)

Realizes platform-isolation **D26** (loader shims) and **D29** (observe seams).
New surface — no C++ `mwin32` antecedent. The session gains an **observation
sink** (a safe trait, default no-op) keyed by `(api, target)` for the D29 volume
policy; the loader policy tables are shim-local (SHIM-D16 first-cut).

- [x] **MW9-1** Module handle table + observation seam: intern real vs
      minted-sentinel `HMODULE` (peer of the SHIM-D3 handle table); add the
      `ObservationSink` trait the session holds (default no-op) plus the
      shim-local `EngineSubstitution` registry and `name→shim-proc` table types
      (empty/seeded). No exports yet; pure unit-tested data structures.
- [x] **MW9-2** `mLoadLibraryW`/`A`, `mLoadLibraryExW`/`A`, `mFreeLibrary`:
      passthrough that observes the load and interns the real `HMODULE`;
      minted-sentinel path wired through the (initially empty) engine-substitution
      registry; `*A` transcode via the `ansi` module (SHIM-D15). Transparent for
      any `HMODULE` not minted here.
- [x] **MW9-3** `mGetProcAddress`: observe `(module, proc)`; consult the
      `name→shim-proc` table (seeded from the current export roster) and return
      the shim body when the mode is not off and the name is shimmed, else
      forward; sentinel modules resolve to shim-supplied procs. Off-mode is a
      pure forward.
- [x] **MW9-4** `mGetModuleHandleW`/`A`, `mGetModuleHandleExW`/`A`: resolve
      previously-minted sentinels by name (else forward); model the `Ex`
      pin/ref-count flags minimally for sentinels. Add all MW9 exports to
      `windows_win32_shim.def` + `windows_win32_shim_aliases.ndjson` (the
      drift-guard test stays green). **The whole loader family is non-opt-in**
      (every export carries a `/alternatename` entry — contrast `mCloseHandle`'s
      `noalias`); correctness rests on the transparency-for-non-minted-values
      invariant, not on opting out.
- [x] **MW9-5** *(integration)* Link-proof + behavior test: a client that
      resolves a shimmed API via genuine `GetProcAddress` (redirected to
      `mGetProcAddress`) lands in the shim body; an engine-substitution sentinel
      returns a shim proc; observation records the resolutions; off-mode is
      byte-for-byte transparent (`dumpbin /imports` confirms `LoadLibraryW` /
      `GetProcAddress` bind the shim).

## MW10 — COM activation shims (`mCoCreateInstance` / `mCoGetClassObject` / …, SHIM-D17)

Realizes the COM half of platform-isolation **D24/D29**. New surface — no C++
`mwin32` antecedent.

> **CROSS-MILESTONE PREREQUISITE:** MW10 reuses the session mode + observation
> sink introduced in **MW9-1**; do MW9 first.

- [x] **MW10-1** Minimal COM plumbing: `IUnknown` / `IClassFactory` vtable
      scaffolding in the `#[allow(unsafe_code)]` boundary module (raw
      `windows-sys` GUID / HRESULT), plus a safe `ShimClassFactory` trait and a
      `CLSID→factory` registry the session can populate. Unit-tested via a stub
      factory.
- [x] **MW10-2** `mCoCreateInstance` + `mCoCreateInstanceEx`: off→forward;
      observe `(CLSID, IID, CLSCTX)`; substitute via the registry when a factory
      is registered; map factory / `QueryInterface` failures to the correct
      `HRESULT`.
- [x] **MW10-3** `mCoGetClassObject`: off→forward; observe; return a shim class
      factory for a registered CLSID, else forward to real activation.
- [x] **MW10-4** Passthrough lifecycle exports (`mCoInitialize` /
      `mCoInitializeEx` / `mCoUninitialize`) + add all MW10 exports to the dual
      manifests (drift-guard green). **All COM exports are non-opt-in**
      (manifest'd with `/alternatename`, like the loader family, not `noalias`);
      transparent when no factory is registered, with volume-aware observation
      per D29.
- [x] **MW10-5** *(integration)* Test: a registered CLSID activates a
      shim-supplied object implementing the requested interface (replay path); an
      unregistered CLSID forwards to real activation (passthrough); activations
      are observed; off-mode forwards unchanged.

## MW11 — In-process module bootstrap + web-host activation seam (ABI, SHIM-D18)

Gets the shim DLL loaded into the web host and inserts a controlled request
handler at the host's **public activation seam**, using the loader/COM
interception (D24/D26, MW9/MW10). In-process replacement for the former
out-of-process HWC pipeline (platform-isolation D17, superseded). Public SDK
names only — no host-specific identifiers.

> **CROSS-COMPONENT PREREQUISITE:** consumes `windows-platform-isolation` → **M8**
> (`RequestHandler` surface + identity/journaling decorators). See
> [`../windows-platform-isolation/CHECKLIST.md`](../windows-platform-isolation/CHECKLIST.md).

- [x] **MW11-1** Confirm load: the shim cdylib is a load-time dependency of the
      relinked host module (via the aliasobj relink, MW5) and its initializer
      runs in the host process; a smoke export proves we are resident.
- [x] **MW11-2** Identify the public activation seam in IIS/HWC terms — a native
      module registration entry (`RegisterModule` →
      `IHttpModuleRegistrationInfo::SetRequestNotifications` → `CHttpModule`)
      and/or a handler-factory acquisition import — using only public SDK names.
      Record the chosen seam in SHIM-D18.
- [x] **MW11-3** Intercept the seam: alias the factory-acquisition import (D24,
      MW9/MW10 machinery) or register our module factory, so the host obtains a
      shim-controlled handler. First cut returns a handler that forwards to the
      real one (pass-through).
- [x] **MW11-4** Add any new exports to the dual manifests (`.def` + `.ndjson`,
      drift-guard green); set their aliasing posture (non-opt-in, consistent with
      MW9/MW10).
- [x] **MW11-5** *(integration)* In an emulated host harness, the interception
      hands back our handler and the host drives it; assert pass-through behavior
      and that our code is on the call path.

## MW12 — Response-path bridge to the safe handler surface (ABI, SHIM-D18)

Bridges the host's per-request calls to the safe `RequestHandler` surface
(platform-isolation M8) through unsafe vtable glue, wiring the identity decorator
so today's behavior is unchanged — the "code in the response path, no behavior
change today" endpoint.

> **CROSS-COMPONENT PREREQUISITE:** `windows-platform-isolation` → **M8**
> (handler surface + identity decorator); builds on **MW11**.

- [x] **MW12-1** Unsafe vtable bridge: translate the host's per-request
      notifications (`OnBeginRequest` / `OnSendResponse`, raw `IHttpContext` /
      request-response pointers) into borrowed models for the safe trait, in the
      `#[allow(unsafe_code)]` boundary module (SHIM-D2).
- [x] **MW12-2** Wire the platform-isolation `IdentityHandler` as the active
      handler (D25 off): forward every notification to the real handler, return
      its disposition unchanged.
- [x] **MW12-3** Map handler dispositions back to host notification return codes /
      `HRESULT`; ensure error and continue/finish outcomes round-trip exactly.
- [x] **MW12-4** Optional journaling path: swap in the platform-isolation
      `JournalingHandler` under record mode, observing each request without
      changing the response (D29 volume policy applies).
- [x] **MW12-5** *(integration)* End-to-end: a request flows through a real or
      emulated host into our bridge and the identity decorator and back; assert
      the response is byte-identical to the un-shimmed path.

## MW13 — `wordy`: a shim-unaware Rust HWC dictionary service (synchronous surface, SHIM-D19)

A standalone, **shim-unaware** native IIS module + host activator that serves a
"shared dictionary" REST API synchronously under genuine HWC, with the word
business-logic fully unit-testable off-host. No isolation, no async yet (those
are MW14 / MW15). `wordy` is a sibling crate (`crates/wordy`); its source carries
zero isolation awareness (SHIM-D19) — it declares its **own** modeled IIS vtable
subset (peer of `mwinweb`), never depending on this crate.

- [x] **MW13-1** Scaffold `crates/wordy` (add to workspace): a `cdylib`+`rlib`
      IIS native-module crate with a generic **env-driven `build.rs`** (SHIM-D19:
      links an extra object + lib search dir only when `WORDY_EXTRA_LINK_*` env
      vars are set, else a plain build — no isolation knowledge). Its own
      `#[allow(unsafe_code)]` IIS-ABI boundary module declaring the minimal
      native-module vtables (`IHttpModuleRegistrationInfo` subset,
      `IHttpModuleFactory`, `CHttpModule`, `IHttpContext`/`IHttpRequest` read of
      method+URL, `IHttpResponse` **status** write) — a peer of `mwinweb`, never
      depending on this crate. Export `RegisterModule`; the factory vends a
      `CHttpModule` whose `OnBeginRequest` runs a safe route dispatcher seed
      (`GET /healthz` → 200, else continue). `wordy` `PLANS.md` + brief
      `DESIGN-NOTES.md` (shim-unaware contract). Proven via an emulated-host unit
      test (mirrors `mwinweb`) — no HWC dependency. The genuine-HWC activator is
      MW13-5.
- [x] **MW13-2** Word-domain core (pure Rust, no IIS, no FS): in-memory shared
      dictionary loaded from the vendored SCOWL `en-US` list (+ its license file);
      `Locale` enum (only `en-US` populated); spell-check **membership**; `regex`
      enumeration; **anagram** solver (positional template fixes length, fixed
      letters are free givens, blanks drawn from a supplied letter **multiset**,
      optional **wildcard tiles**); `fst` edit-distance **suggestions** over the
      shared list. Extensive unit tests (≥10 normal + edge cases) — proves the
      business logic runs with **zero host** (the no-HWC end-goal in miniature).
- [x] **MW13-3** Custom-dictionary FS store: per-`{locale}/{user}` directory of
      **name-encoded empty word files**; add / exists / remove / enumerate via
      `std::fs` **namespace/metadata ops only** (no content — SHIM-D6 aligned);
      reversible, path-escape-proof word↔filename encoding (lowercase +
      percent-encode outside `[a-z]`); `Principal`/`UserId` newtype threaded,
      resolved from an `X-Wordy-User` header with a single default user.
      Unit-tested over a scratch temp dir (RAII cleanup).
- [x] **MW13-4** Response **body** write path + route dispatcher: extend `wordy`'s
      IIS boundary to clear/set-status/write a JSON body (`IHttpResponse`); JSON
      request/response models; map every route — `POST /spellcheck`,
      `POST /anagram`, `GET /shared?pattern=`, `GET /custom?pattern=`,
      `POST /custom/{word}`, `DELETE /custom/{word}`, `GET /custom/{word}` — to the
      domain core + FS store, **synchronously**.
- [x] **MW13-5** *(integration)* End-to-end route harness + HWC readiness
      pre-flight. Integration tests (`wordy/tests/host.rs`) drive **every** route
      end-to-end through the public `routes::Service` (the host-agnostic core the
      IIS boundary calls) over a scratch custom-dictionary store at integration
      scale (hundreds of ops), asserting all dictionary behaviors; the IIS ABI
      boundary itself (decode body/header → dispatch → write JSON body) is covered
      by the emulated-host unit tests in `src/iis.rs`. Adds the `wordy-host`
      activator bin (`src/bin/wordy-host.rs`): discovers genuine `hwebcore.dll` at
      the absolute `inetsrv` path, locates the built `wordy.dll`, generates a
      representative applicationHost/web.config loading it, and (opt-in
      `WORDY_HOST_PROBE`) `LoadLibraryExW`s the real engine by absolute path with
      the `inetsrv` dependency dir and resolves its three exports — proving the
      load seam — then frees it. Safe everywhere (exits 0; HWC discovery gated).
      **Genuine `WebCoreActivate` + live HTTP is deferred to MW16** (it requires
      pinning the modeled vtables to real `httpserv.h` first).

      > **➡ HANDOFF:** genuine in-process HWC hosting (this milestone's
      > `WebCoreActivate` + live HTTP, and MW15-2's `hwcproof/`) is blocked on
      > **MW16** (real `httpserv.h` vtable pinning). See MW16 below.

## MW16 — Pin the modeled IIS vtables to `httpserv.h` + genuine HWC activation (SHIM-D19)

> **Re-plan note (execution-driven):** MW13-1 deliberately modeled only the
> *subset* of the IIS native-module vtables `wordy` exercises (WD-D3), in a
> self-consistent ordering sufficient for the emulated host. Driving a **genuine**
> Hostable Web Core process, however, requires those vtables to match the real
> `httpserv.h` memory layout exactly — `CHttpModule` alone declares ~30 ordered
> notification methods, and a real host calling an unmodeled slot at the wrong
> offset would mis-dispatch. Pinning the real layout is therefore a substantial,
> SDK-dependent, crash-sensitive effort that is its own milestone — a shared
> prerequisite of MW13-5's genuine path and MW15-2's `hwcproof/` harness. It was
> separated out of MW13-5 during execution so the rest of MW13 could land runnable.

- [x] **MW16-1** Pin the genuine `httpserv.h` vtable layouts for every interface
      `wordy` touches (`IHttpModuleRegistrationInfo`, `IHttpModuleFactory`,
      `CHttpModule` — all notification slots, `IHttpContext`, `IHttpRequest`,
      `IHttpResponse`), with the unmodeled `CHttpModule` notifications defaulting
      to a safe pass-through, verified against the SDK header.
- [x] **MW16-2** Genuine activation in `wordy-host`: `WebCoreActivate` the real
      `hwebcore.dll` with the generated applicationHost/web.config loading the
      pinned `wordy.dll`, then `WebCoreShutdown`; single-activation-per-process
      and error-code semantics handled per the HWC notes. **Verified on a machine
      with IIS-HostableWebCore: `WebCoreActivate` → `HRESULT 0`, `wordy.dll`
      loads, `RegisterModule` runs, `SetRequestNotifications` → `S_OK`, and IIS
      calls `GetHttpModule` once per request (allocating from the request pool via
      `IModuleAllocator`).** Bin gates: `WORDY_HOST_ACTIVATE` / `WORDY_HOST_HTTP` /
      `WORDY_HOST_DUMP` / `WORDY_HOST_CONFIG`; `iis.rs` gains a `WORDY_TRACE` gated
      trace.
- [x] **MW16-3** *(integration)* Drive every route end-to-end over **real HTTP**
      against the activated host; assert dictionary behaviors; gated/ignored when
      HWC is absent. Reconciles the modeled-vs-genuine boundary and unblocks
      MW15-2.

      > **✅ RESOLVED (see wordy WD-D11).** Genuine HWC now dispatches every route
      > into `wordy` end-to-end (`GET /healthz` → `200 {"status":"ok"}`,
      > `POST /spellcheck` → `200 {"results":[…]}`, all 7 routes → `200`). The
      > earlier bare-`500` was **HTTP 500.19** (`sc-win32-status 1168`,
      > `ERROR_NOT_FOUND`): the hand-rolled `applicationHost.config` declared only
      > a *subset* of the standard `<configSections>`, so IIS aborted each request
      > at config resolution — before the notification pipeline — when a loaded
      > module read an undeclared section (`staticContent`, `httpProtocol`, …).
      > `wordy`'s binding was correct all along (as the emulated-host unit tests
      > showed). Fix: `wordy-host::application_host_config` now emits the
      > **complete** standard section set + the core `inetsrv` pipeline modules.
      > Covered by the `hwc_genuine_http_dispatch_end_to_end` integration test,
      > which drives genuine HWC **by default** on a capable host
      > (`WORDY_HWC_EMULATED_ONLY=1` opts out; skips when HWC is absent or the
      > listener cannot bind without elevation). Diagnosis aids:
      > W3C site logging surfaced the sub-status; `custerr.dll` +
      > `errorMode="Detailed"` named the offending section; per-slot trace
      > trampolines confirmed no notification was dispatched.

## MW14 — Asynchronous request completion on the Windows thread pool (SHIM-D19)

Make **every** route async via IIS asynchronous completion, offloaded to our
`windows-threadpool` crate — the deliberate "force the redirection open across a
second seam" milestone.

- [x] **MW14-1** Extend `wordy`'s IIS-ABI boundary with the async-completion
      subset: `IHttpContext::PostCompletion` (and the `RQ_NOTIFICATION_PENDING`
      return); boundary module only, unit-modeled.
- [x] **MW14-2** Async dispatch: `OnBeginRequest` submits the route's work to
      `windows_threadpool::submit_once`, returns `RQ_NOTIFICATION_PENDING`; the
      pool work item computes, writes the response, and calls `PostCompletion`.
      Route every endpoint through it (add the `windows-threadpool` path dep).
      *(Landed with MW14-3 — the async `OnBeginRequest` returns `PENDING` and
      writes nothing synchronously, so it cannot be tested without the
      suspend/resume harness; the two are coupled. Design refinement per WD-D12:
      the response is realized in `OnAsyncCompletion` on the host thread, not the
      pool work item, so all `IHttpResponse` calls stay on the host thread.)*
- [x] **MW14-3** Extend the emulated-host harness to model **suspend/resume**
      (deliver `PENDING`, run the completion, finalize) so async routes are
      testable without HWC. *(Landed with MW14-2; see WD-D12.)*
- [x] **MW14-4** Concurrency hardening: shared dictionary as a read-only `Arc`;
      per-user custom-store FS ops serialized or concurrency-tolerant; verify no
      data races or handle-lifetime issues across the pool boundary.
      *(Shared dict is a `&'static` `LazyLock` singleton — read-only + `Sync`,
      stronger than `Arc`. The concurrency test exposed a real Windows
      concurrent-create `ACCESS_DENIED` race that retrying did not reliably clear,
      so custom-store mutations are now serialized by a per-store `Arc<Mutex<()>>`
      (reads stay lock-free). Pool-boundary safety per WD-D12. See WD-D12.)*
- [x] **MW14-5** *(integration)* Async end-to-end: many concurrent requests across
      routes; assert correctness, that work ran off the host thread (observation
      marker), and clean completion. (`windows-threadpool-executor` async/await
      variant noted as a follow-on, not required here.)
      *(Drives 240 concurrent requests across every route through the real OS
      thread pool — the same pool the IIS async boundary offloads to — asserting
      each response is correct, that no work ran on the host thread (thread-id
      observation), and that every work item joined cleanly.)*

## MW15 — Isolation proof: force the redirection open (SHIM-D19)

Applies the alias + `.pilcfg` to the *unmodified* `wordy` from the outside
(SHIM-D19) and proves its namespace ops land in the overlay, not the live FS.

> **Re-plan note (scheduling, 2026-06-25):** detailing the outline revealed the
> blocking prerequisite that the header's *"isolation deferred"* foreshadowed:
> the shim's filesystem surface is hardcoded to `LiveFilesystem` passthrough —
> there is no `FilesystemBacking` and `.pilcfg`-driven FS layering is the
> documented SHIM-D13 gap. Since `wordy`'s custom store is filesystem-based, the
> overlay proof cannot run until that gap is closed. The building blocks already
> exist in `windows-platform-isolation` (`FsSurface`, `OverlayFileTree`,
> `TreeFsSurface`), so closing it is bounded. The original four items are
> renumbered MW15-3..6; MW15-1/2 are the prerequisite. Decision: overlay-over-live
> semantics, gated by the existing `buffer_updates` flag (now "buffer all
> mutations: registry + filesystem").

- [ ] **MW15-1** Add an `FsBuffered<S: FsSurface, C>` decorator to
      `windows-platform-isolation` — the filesystem analogue of the registry
      `Buffered`: mutations land in an in-memory overlay (with tombstones that
      shadow inner/live paths) and never reach the inner surface; reads see the
      overlay layered over the inner (read-your-writes); `commit` replays the
      journal. Unit-tested over a `LiveFilesystem` and/or `TreeFsSurface` base
      (create/remove/enumerate land in the overlay; inner untouched). Record the
      decision in the platform-isolation design notes.
- [ ] **MW15-2** Wire a `FilesystemBacking` enum (`Live` / `Buffered`) into the
      shim `ShimSession`, selected from `.pilcfg` (`buffer_updates` now buffers
      filesystem mutations too); change `session.filesystem` to the enum. Shim
      integration tests drive the FS C ABI through the buffered backing and assert
      namespace ops land in the overlay with the live FS untouched. Update
      SHIM-D13.
- [ ] **MW15-3** Build `wordy` with the alias `.obj` + shim import lib injected via
      the generic `build.rs` env vars (no `wordy` source change); confirm via
      `dumpbin /imports` that the FS + thread-pool/loader imports bind the shim.
      (Orchestration script mirroring `linkproof/run-linkproof.ps1`.)
- [ ] **MW15-4** `hwcproof/` harness (mirrors `linkproof/`): genuine HWC + a
      buffered `.pilcfg` beside the aliased `wordy.dll`; real HTTP add/remove/
      enumerate of custom words; assert the namespace ops land in the shim
      overlay, not the live FS.
- [ ] **MW15-5** Negative control: a non-aliased `wordy` hits the live FS; an
      exit-code discriminator distinguishes the two builds.
- [ ] **MW15-6** *(integration)* End-to-end isolation proof, gated/ignored when HWC
      is absent; record closure / any new decisions in SHIM-D19.


