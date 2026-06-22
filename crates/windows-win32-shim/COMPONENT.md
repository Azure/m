# windows-win32-shim

A parallel, **all-Rust** reimplementation of the C++ `mwin32` DLL
(`src/Windows/libraries/mwin32/`). It exposes a Win32-shaped C ABI (the
`m`-prefixed entry points — `mRegOpenKeyExW`, `mCreateFileW`, …) whose bodies
route through the Rust `windows-platform-isolation` crate instead of the live OS
API directly.

This crate is **not** layered on the C++ implementation; it is an independent
Rust stack that targets the same exported ABI and the same `.pilcfg` /
saved-state artifacts (artifact parity per platform-isolation D5).

Current scope: **filesystem and registry only.** HWC and other Win32 surfaces
are out of scope for now.

This directory is a source-component (marked by this `COMPONENT.md`). See
`CHECKLIST.md` / `PLANS.md` for the milestone plan and `DESIGN-NOTES.md`
(`SHIM-D` decisions) for the rationale.
