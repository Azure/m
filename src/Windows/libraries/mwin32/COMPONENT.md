# mwin32 source-component

`mwin32` is a Windows-only drop-in replacement DLL for a subset of the Win32 API,
starting with the Windows registry. Every `mReg*` entry point is a thin shim that
redirects into the `m` package's `pil` (platform isolation library), so the same
client code can run in one of several modes selected by the active PIL stack:

- **(a) passthrough** — calls flow straight through to the live Win32 registry.
- **(b) logging** — calls are recorded (PIL `record_modifications`) and can be
  written out for inspection.
- **(c) buffered** — registry state is buffered away from the live system
  (PIL `buffer_updates`), can be persisted, and later reloaded so a program runs
  against captured state without touching the running machine.

Mode selection auto-configures to **passthrough** unless a sidecar configuration
file named `<executable>.pilcfg` is found next to the host executable, in which
case it is parsed (JSON) to describe the PIL stack.

## Redirecting unmodified clients: the `mwin32_alias` link object

A client that already calls the genuine Win32 registry API (`RegCreateKeyExW`,
`RegSetValueExW`, `RegCloseKey`, …) can be redirected into this shim **without any
source change** by linking the `mwin32_alias` CMake OBJECT library:

```cmake
target_link_libraries(my_client PRIVATE mwin32_alias)
```

`mwin32_alias` contains no logic. For every shim export it defines the matching
`__imp_<Win32Name>` import-address-table slot and points it at the shim, so a
client's `<windows.h>` `__declspec(dllimport)` call lands in `mReg*` instead of
advapi32. The slot set is generated from `mwin32.def`, so it can never drift from
the shim's exports. Linking the object transitively brings in `m_mwin32.dll`, and
the usual `<executable>.pilcfg` sidecar selects the mode (passthrough / logging /
buffered) as above.

**What it redirects, and what it cannot.** This is a deliberately shallow,
supported, link-time mechanism — it redirects the registry calls the client itself
*links*. It does **not** redirect:

- calls made through `GetProcAddress` / `LoadLibrary` (resolved at runtime, not via
  the IAT slot the alias defines);
- calls already compiled into a **third-party static library** that hard-references
  advapi32's own `__imp_` slots;
- the advapi32 → kernelbase API-set layering beneath the public names.

When `advapi32.lib` is on the link line it wins any plain (non-`dllimport`)
reference; the `__imp_` slot is the reliable path and real `<windows.h>` clients
always take it. Reaching the cases above is a runtime-interception (Detours)
envelope, evaluated separately. See `DESIGN-NOTES.md` D8.

See `CHECKLIST.md` / `PLANS.md` for in-progress work and `DESIGN-NOTES.md` for
design decisions.
