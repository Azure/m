# windows-win32-shim — PLANS

Tracks CHECKLIST.md files in this source-component and their status.

| Path to CHECKLIST.md | Status | Brief description | Design Notes |
|---|---|---|---|
| [CHECKLIST.md](CHECKLIST.md) | completed | Parallel all-Rust `mwin32` shim: Win32-shaped C ABI routed through `windows-platform-isolation`, filesystem + registry only. MW1 foundation **(done)** — cdylib/ABI posture, error mapping, handle table, session; MW2 registry W ABI **(done)** — value codec, surface-generic reg core, `mReg*W` entry points + NOT_SUPPORTED stubs; MW3 filesystem W ABI **(done)** — surface-generic fs core (`fs_ops`), `m*W` metadata/dir/enum entry points (`mwinfile`) + content/move/copy NOT_SUPPORTED stubs, session filesystem facade; MW4 `.pilcfg` JSON sidecar **(done)** — strict `pilcfg` parse + env-expansion, tolerant `load_pilcfg`, config-driven `RegistryBacking` (persisted/buffered/live) + best-effort `capture_snapshot` (SHIM-D13); MW5 link-time Win32→m alias **(done — MW5-1 `.def` source-of-truth + MW5-2 `alias_gen` C++-text generator; MW5-3 `windows_win32_shim_aliases.ndjson` manifest + MW5-4 `alias_obj` pure-Rust COFF emitter (`object` crate: `__imp_` slots + `ADDR64` relocs + `.drectve`, no MSVC tool) + MW5-5 `gen-alias-obj` CLI, all unit-tested incl. `.def`↔`.ndjson` drift guard; MW5-6 C++ link-proof (`linkproof/`, reuses the mwin32 C++ link-proof program) verified via `dumpbin /imports` + a buffered/live exit-code discriminator — see [COMPLETED-CHECKLIST.md](COMPLETED-CHECKLIST.md))**, MW6 ANSI forms **(done — MW6-1 `ansi` `CP_ACP` boundary module (`Utf16::{from_code_page,to_code_page}` via `windows-text`); MW6-2 registry `A` entry points (10) + MW6-3 filesystem `A` entry points (11) over the shared `reg_ops`/`fs_ops` cores (SHIM-D15, observable-parity set only; `NOT_SUPPORTED` `W` stubs get no `A` spelling); MW6-4 `A`/`W` parity integration tests (`tests/ansi_parity.rs`) — see [COMPLETED-CHECKLIST.md](COMPLETED-CHECKLIST.md) once migrated)**, MW7 end-to-end/C++ registry-artifact parity **(done — shim-level C++-dialect `persisted_state` parity (`testdata/cpp_registry_artifact.xml` + `tests/cpp_parity.rs`, SHIM-D20), packaging/deployment doc (SHIM-D21), and a single-`.pilcfg` registry+filesystem isolation capstone)**; MW8 `FindFirstFileEx` family completeness **(done — see [COMPLETED-CHECKLIST.md](COMPLETED-CHECKLIST.md))** — applies leaf/wildcard matching (closes SHIM-D12 over-match) via the `windows-text` matcher (WT-6), adds `mFindFirstFileExW`/`mFindFirstFileTransactedW` + search-op/info-level/flag handling (SHIM-D14; emits the 8.3 short name sourced from isolation M10, suppressed for `FindExInfoBasic`), with `A` forms folded into MW6-3 and alias exports into MW5-1 | [DESIGN-NOTES.md](DESIGN-NOTES.md) |

## Newly planned milestones

- **MW9 — Dynamic-loader shims (done):** `mLoadLibrary*` / `mGetProcAddress`
  / `mFreeLibrary` / `mGetModuleHandle*`, a module handle table, a name→shim-proc
  redirection table, an engine-substitution sentinel, and a session observation
  sink (D29). Realizes platform-isolation **D26** (SHIM-D16). New surface — no
  C++ `mwin32` antecedent. Off-mode is byte-for-byte transparent; the loader
  family is non-opt-in in the alias roster; behavior proven end-to-end in
  `tests/loader.rs`.
- **MW10 — COM activation shims (done):** `mCoCreateInstance` /
  `mCoCreateInstanceEx` / `mCoGetClassObject` (+ passthrough `mCoInitialize*` /
  `mCoUninitialize`), a CLSID→factory substitution registry, and minimal
  `IUnknown`/`IClassFactory` plumbing (SHIM-D17). Depends on MW9 for the
  mode/observation seam. New surface — no C++ `mwin32` antecedent.
- **MW11 — In-process module bootstrap + web-host activation seam (done):**
  load the shim cdylib into the web host (aliasobj relink), intercept the public
  activation seam (`RegisterModule` / handler-factory acquisition import, D24/D26)
  and hand back a shim-controlled pass-through handler (SHIM-D18). Consumes
  platform-isolation **M8**.
- **MW12 — Response-path bridge to the safe handler surface (done):**
  unsafe vtable bridge translating the host's per-request notifications into the
  safe `RequestHandler` trait, wiring the identity decorator so behavior is
  unchanged today (SHIM-D18). Builds on MW11; consumes platform-isolation **M8**.
  Together MW11/MW12 are the in-process replacement for the former out-of-process
  HWC pipeline (platform-isolation D17, deferred).
- **MW13 — `wordy` synchronous dictionary service (planned):** a shim-unaware
  sibling crate (`crates/wordy`) — a Rust IIS native-module REST "shared
  dictionary" service (spell-check, custom-dict add/remove, regex enumeration,
  anagram solver, `fst` suggestions) + a `wordy-host` HWC activator. Serves the
  REST surface **synchronously** under genuine HWC; word business-logic fully
  unit-testable off-host. Custom dictionary = name-encoded files (SHIM-D6
  namespace surface); per-user/per-locale forward-compatible (SHIM-D19). `wordy`
  declares its **own** modeled IIS vtables and never depends on this crate.
- **MW14 — Asynchronous completion on the Windows thread pool (planned):** every
  route async via IIS `RQ_NOTIFICATION_PENDING` + `IHttpContext::PostCompletion`,
  offloaded to `windows-threadpool::submit_once`; emulated host extended to model
  suspend/resume. Deliberately forces the redirection open across a second seam
  beyond the filesystem (SHIM-D19).
- **MW15 — Isolation proof (done):** apply the alias `.obj` + `.pilcfg` to the
  *unmodified* `wordy` from the outside and prove its namespace ops land in the
  overlay, not the live FS. `FsBuffered` overlay-over-live decorator
  (platform-isolation D30) + a `FilesystemBacking` enum wired into the shim
  session so `buffer_updates` now buffers the filesystem too (SHIM-D13).
  `hwcproof/build-aliased-wordy.ps1` proves link-time redirection statically
  (`dumpbin`); `hwcproof/run-hwcproof.ps1` proves it at runtime under **genuine
  HWC** (isolated variant: live custom root never created on disk; native
  control: it is), gated by `tests/hwc_isolation.rs` (`#[ignore]`d, skips when
  HWC absent / URL unbindable). Closure recorded in SHIM-D19.
- **MW17 — WinHTTP egress seam (done, SHIM-D22):** alias an unmodified app's
  `winhttp.dll` imports to `m`-prefixed front-ends that reassemble the `HINTERNET`
  request lifecycle into one `EgressRequest`, run it through a session
  `EgressBacking` selected from a `.pilcfg` `egress` section (passthrough / redirect
  / buffer / replay / block), and drain the response back. Consumes
  `windows-platform-isolation` **M11** (D31). HTTP only; WWSAPI/SOAP deferred.
  Proven by an `egressproof/` harness + negative control.
- **MW18 — Validation tier (done, SHIM-D23):** split `wordy` so its on-disk
  custom dictionary lives in a new **`merriam`** REST service (HTTP Server API
  inbound) backed by a new **`windows-file-io`** crate (native async Win32 overlapped
  I/O + thread-pool completion); `wordy` relays custom-dict ops to `merriam` over
  WinHTTP (staying shim-unaware), making that relay the egress MW17 isolates. End-to-
  end proof of redirect / buffer / replay against the real service.

First cut keeps the loader/COM substitution registries and observation sink
**shim-local**; no new `windows-platform-isolation` surface is introduced (the
shim consults the session for mode + sink, as the registry/filesystem surfaces
do). Promotion to a shared surface is deferred per "design notes are not a work
queue".

## Cross-component dependency

MW3 (filesystem C ABI) depends on `windows-platform-isolation` → **M9**
(`LiveFilesystem` provider). See
[`../windows-platform-isolation/CHECKLIST.md`](../windows-platform-isolation/CHECKLIST.md).

MW8 (find-Ex wildcard matching) depends on `windows-text` → **WTM-1**
(`name_matches_expression`, WT-6). See
[`../windows-text/CHECKLIST.md`](../windows-text/CHECKLIST.md).

MW8 (8.3 short-name passthrough) depends on `windows-platform-isolation` →
**M10** (`DirEntry.short_name`, D23). See
[`../windows-platform-isolation/CHECKLIST.md`](../windows-platform-isolation/CHECKLIST.md).

MW11/MW12 (web-host response-path module) depend on `windows-platform-isolation`
→ **M8** (`RequestHandler` surface + identity/journaling decorators). See
[`../windows-platform-isolation/CHECKLIST.md`](../windows-platform-isolation/CHECKLIST.md).

MW14 (`wordy` async completion) depends on `windows-threadpool` (path dep) for
`submit_once`; MW13 vendors a SCOWL `en-US` word list and uses the `fst` +
`regex` crates. `wordy` itself stays **shim-unaware** — it does not depend on
`windows-win32-shim` (SHIM-D19).
