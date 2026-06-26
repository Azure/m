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

_No pending items. All milestones (MW1—MW18) are complete; the archived groups live in [COMPLETED-CHECKLIST.md](COMPLETED-CHECKLIST.md)._
