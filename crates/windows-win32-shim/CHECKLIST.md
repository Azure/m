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
- [ ] **MW11-2** Identify the public activation seam in IIS/HWC terms — a native
      module registration entry (`RegisterModule` →
      `IHttpModuleRegistrationInfo::SetRequestNotifications` → `CHttpModule`)
      and/or a handler-factory acquisition import — using only public SDK names.
      Record the chosen seam in SHIM-D18.
- [ ] **MW11-3** Intercept the seam: alias the factory-acquisition import (D24,
      MW9/MW10 machinery) or register our module factory, so the host obtains a
      shim-controlled handler. First cut returns a handler that forwards to the
      real one (pass-through).
- [ ] **MW11-4** Add any new exports to the dual manifests (`.def` + `.ndjson`,
      drift-guard green); set their aliasing posture (non-opt-in, consistent with
      MW9/MW10).
- [ ] **MW11-5** *(integration)* In an emulated host harness, the interception
      hands back our handler and the host drives it; assert pass-through behavior
      and that our code is on the call path.

## MW12 — Response-path bridge to the safe handler surface (ABI, SHIM-D18)

Bridges the host's per-request calls to the safe `RequestHandler` surface
(platform-isolation M8) through unsafe vtable glue, wiring the identity decorator
so today's behavior is unchanged — the "code in the response path, no behavior
change today" endpoint.

> **CROSS-COMPONENT PREREQUISITE:** `windows-platform-isolation` → **M8**
> (handler surface + identity decorator); builds on **MW11**.

- [ ] **MW12-1** Unsafe vtable bridge: translate the host's per-request
      notifications (`OnBeginRequest` / `OnSendResponse`, raw `IHttpContext` /
      request-response pointers) into borrowed models for the safe trait, in the
      `#[allow(unsafe_code)]` boundary module (SHIM-D2).
- [ ] **MW12-2** Wire the platform-isolation `IdentityHandler` as the active
      handler (D25 off): forward every notification to the real handler, return
      its disposition unchanged.
- [ ] **MW12-3** Map handler dispositions back to host notification return codes /
      `HRESULT`; ensure error and continue/finish outcomes round-trip exactly.
- [ ] **MW12-4** Optional journaling path: swap in the platform-isolation
      `JournalingHandler` under record mode, observing each request without
      changing the response (D29 volume policy applies).
- [ ] **MW12-5** *(integration)* End-to-end: a request flows through a real or
      emulated host into our bridge and the identity decorator and back; assert
      the response is byte-identical to the un-shimmed path.

