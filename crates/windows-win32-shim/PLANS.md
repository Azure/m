# windows-win32-shim — PLANS

Tracks CHECKLIST.md files in this source-component and their status.

| Path to CHECKLIST.md | Status | Brief description | Design Notes |
|---|---|---|---|
| [CHECKLIST.md](CHECKLIST.md) | in progress | Parallel all-Rust `mwin32` shim: Win32-shaped C ABI routed through `windows-platform-isolation`, filesystem + registry only. MW1 foundation **(done)** — cdylib/ABI posture, error mapping, handle table, session; MW2 registry W ABI **(done)** — value codec, surface-generic reg core, `mReg*W` entry points + NOT_SUPPORTED stubs; MW3 filesystem W ABI **(done)** — surface-generic fs core (`fs_ops`), `m*W` metadata/dir/enum entry points (`mwinfile`) + content/move/copy NOT_SUPPORTED stubs, session filesystem facade; MW4 `.pilcfg` JSON sidecar **(done)** — strict `pilcfg` parse + env-expansion, tolerant `load_pilcfg`, config-driven `RegistryBacking` (persisted/buffered/live) + best-effort `capture_snapshot` (SHIM-D13); MW5 link-time Win32→m alias **(core done — MW5-1 `.def` source-of-truth + MW5-2 `alias_gen` generator with unit tests; MW5-3 C++ link-proof deferred as cross-toolchain)**, MW6 ANSI forms (outline), MW7 end-to-end/C++ artifact parity (outline); MW8 `FindFirstFileEx` family completeness **(done — see [COMPLETED-CHECKLIST.md](COMPLETED-CHECKLIST.md))** — applies leaf/wildcard matching (closes SHIM-D12 over-match) via the `windows-text` matcher (WT-6), adds `mFindFirstFileExW`/`mFindFirstFileTransactedW` + search-op/info-level/flag handling (SHIM-D14; emits the 8.3 short name sourced from isolation M10, suppressed for `FindExInfoBasic`), with `A` forms folded into MW6-3 and alias exports into MW5-1 | [DESIGN-NOTES.md](DESIGN-NOTES.md) |

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
