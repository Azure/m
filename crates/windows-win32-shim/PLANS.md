# windows-win32-shim — PLANS

Tracks CHECKLIST.md files in this source-component and their status.

| Path to CHECKLIST.md | Status | Brief description | Design Notes |
|---|---|---|---|
| [CHECKLIST.md](CHECKLIST.md) | in progress | Parallel all-Rust `mwin32` shim: Win32-shaped C ABI routed through `windows-platform-isolation`, filesystem + registry only. MW1 foundation **(done)** — cdylib/ABI posture, error mapping, handle table, session; MW2 registry W ABI **(done)** — value codec, surface-generic reg core, `mReg*W` entry points + NOT_SUPPORTED stubs; MW3 filesystem W ABI **(done)** — surface-generic fs core (`fs_ops`), `m*W` metadata/dir/enum entry points (`mwinfile`) + content/move/copy NOT_SUPPORTED stubs, session filesystem facade; MW4 `.pilcfg` JSON sidecar **(done)** — strict `pilcfg` parse + env-expansion, tolerant `load_pilcfg`, config-driven `RegistryBacking` (persisted/buffered/live) + best-effort `capture_snapshot` (SHIM-D13); MW5 link-time Win32→m alias **(done — MW5-1 `.def` source-of-truth + MW5-2 `alias_gen` C++-text generator; MW5-3 `windows_win32_shim_aliases.ndjson` manifest + MW5-4 `alias_obj` pure-Rust COFF emitter (`object` crate: `__imp_` slots + `ADDR64` relocs + `.drectve`, no MSVC tool) + MW5-5 `gen-alias-obj` CLI, all unit-tested incl. `.def`↔`.ndjson` drift guard; MW5-6 C++ link-proof (`linkproof/`, reuses the mwin32 C++ link-proof program) verified via `dumpbin /imports` + a buffered/live exit-code discriminator — see [COMPLETED-CHECKLIST.md](COMPLETED-CHECKLIST.md))**, MW6 ANSI forms **(done — MW6-1 `ansi` `CP_ACP` boundary module (`Utf16::{from_code_page,to_code_page}` via `windows-text`); MW6-2 registry `A` entry points (10) + MW6-3 filesystem `A` entry points (11) over the shared `reg_ops`/`fs_ops` cores (SHIM-D15, observable-parity set only; `NOT_SUPPORTED` `W` stubs get no `A` spelling); MW6-4 `A`/`W` parity integration tests (`tests/ansi_parity.rs`) — see [COMPLETED-CHECKLIST.md](COMPLETED-CHECKLIST.md) once migrated)**, MW7 end-to-end/C++ artifact parity (outline); MW8 `FindFirstFileEx` family completeness **(done — see [COMPLETED-CHECKLIST.md](COMPLETED-CHECKLIST.md))** — applies leaf/wildcard matching (closes SHIM-D12 over-match) via the `windows-text` matcher (WT-6), adds `mFindFirstFileExW`/`mFindFirstFileTransactedW` + search-op/info-level/flag handling (SHIM-D14; emits the 8.3 short name sourced from isolation M10, suppressed for `FindExInfoBasic`), with `A` forms folded into MW6-3 and alias exports into MW5-1 | [DESIGN-NOTES.md](DESIGN-NOTES.md) |

## Newly planned milestones

- **MW9 — Dynamic-loader shims (not started):** `mLoadLibrary*` / `mGetProcAddress`
  / `mFreeLibrary` / `mGetModuleHandle*`, a module handle table, a name→shim-proc
  redirection table, an engine-substitution sentinel, and a session observation
  sink (D29). Realizes platform-isolation **D26** (SHIM-D16). New surface — no
  C++ `mwin32` antecedent.
- **MW10 — COM activation shims (not started):** `mCoCreateInstance` /
  `mCoCreateInstanceEx` / `mCoGetClassObject` (+ passthrough `mCoInitialize*` /
  `mCoUninitialize`), a CLSID→factory substitution registry, and minimal
  `IUnknown`/`IClassFactory` plumbing (SHIM-D17). Depends on MW9 for the
  mode/observation seam. New surface — no C++ `mwin32` antecedent.
- **MW11 — In-process module bootstrap + web-host activation seam (not started):**
  load the shim cdylib into the web host (aliasobj relink), intercept the public
  activation seam (`RegisterModule` / handler-factory acquisition import, D24/D26)
  and hand back a shim-controlled pass-through handler (SHIM-D18). Consumes
  platform-isolation **M8**.
- **MW12 — Response-path bridge to the safe handler surface (not started):**
  unsafe vtable bridge translating the host's per-request notifications into the
  safe `RequestHandler` trait, wiring the identity decorator so behavior is
  unchanged today (SHIM-D18). Builds on MW11; consumes platform-isolation **M8**.
  Together MW11/MW12 are the in-process replacement for the former out-of-process
  HWC pipeline (platform-isolation D17, deferred).

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
