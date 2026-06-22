# windows-win32-shim

A parallel, **all-Rust** reimplementation of the C++ `mwin32` DLL
(`src/Windows/libraries/mwin32/`). It exposes a Win32-shaped C ABI (the
`m`-prefixed entry points — `mRegOpenKeyExW`, `mCreateFileW`, …) whose bodies
route through the Rust `windows-platform-isolation` crate instead of the live OS
API directly.

This crate is **not** layered on the C++ implementation; it is an independent
Rust stack that targets the same exported ABI and the same `.pilcfg` /
saved-state artifacts (artifact parity per platform-isolation D5).

Current scope: **filesystem and registry only.** It is Windows-only; on other
platforms it compiles to nothing.

## Layout

- `src/error_map.rs` — translation of `windows-platform-isolation`
  `RegistryError` / `FilesystemError` into Win32 `LSTATUS` / error codes, plus a
  `set_last_error` helper (SHIM-D7).
- `src/handle_table.rs` — the minted-handle table that hands out `HANDLE` /
  `HKEY` values with the reserved bit pattern and interns registry-key, file,
  and find-enumeration state (SHIM-D3).
- `src/session.rs` — the process-wide session that vends the registry facade,
  defaulting to live passthrough (SHIM-D8).

The crate builds as both a `cdylib` (the shipped ABI surface) and an `rlib`
(so in-crate integration tests can exercise the foundation API directly).

See `CHECKLIST.md` / `PLANS.md` for the milestone plan and `DESIGN-NOTES.md`
(`SHIM-D` decisions) for the rationale.
