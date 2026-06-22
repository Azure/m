# windows-win32-shim — PLANS

Tracks CHECKLIST.md files in this source-component and their status.

| Path to CHECKLIST.md | Status | Brief description | Design Notes |
|---|---|---|---|
| [CHECKLIST.md](CHECKLIST.md) | in progress | Parallel all-Rust `mwin32` shim: Win32-shaped C ABI routed through `windows-platform-isolation`, filesystem + registry only. MW1 foundation **(done)** — cdylib/ABI posture, error mapping, handle table, session; MW2 registry W ABI **(done)** — value codec, surface-generic reg core, `mReg*W` entry points + NOT_SUPPORTED stubs; MW3 filesystem W ABI (needs isolation M9 live FS provider), MW4 `.pilcfg` JSON sidecar, MW5 link-time Win32→m alias, MW6 ANSI forms (outline), MW7 end-to-end/C++ artifact parity (outline) | [DESIGN-NOTES.md](DESIGN-NOTES.md) |

## Cross-component dependency

MW3 (filesystem C ABI) depends on `windows-platform-isolation` → **M9**
(`LiveFilesystem` provider). See
[`../windows-platform-isolation/CHECKLIST.md`](../windows-platform-isolation/CHECKLIST.md).
