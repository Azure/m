# wordy

A shim-unaware Rust IIS native-module REST service — a "shared dictionary"
server. `wordy` exists in this repository to be a realistic third-party
Hostable Web Core (HWC) application:

1. a proof harness for the link-time host-call redirection developed in
   [`windows-win32-shim`](../windows-win32-shim) (SHIM-D19), and
2. a place to grow genuine HWC business logic on an unprivileged developer
   machine.

Crucially, `wordy` knows **nothing** about the isolation machinery. It has no
dependency on `windows-win32-shim`, contains no isolation code, and builds as an
ordinary IIS native module. Isolation, when applied, is performed entirely from
the outside (see `DESIGN-NOTES.md`).

## Layout

- `src/routes.rs` — the pure, safe, platform-independent request → outcome logic.
- `src/iis.rs` (Windows only) — the `#[allow(unsafe_code)]` native-module ABI
  boundary that bridges the IIS host into `routes`. A peer of
  `windows-win32-shim`'s `mwinweb`; it declares its own modeled vtables and never
  depends on the shim.

## Status

MW13-1 scaffolds the crate with a single health route (`GET /healthz` → 200),
proven via an emulated-host unit test. The full dictionary surface, the FS-backed
per-user custom dictionaries, the async (Windows thread pool) variant, and the
genuine-HWC activator follow in MW13–MW15. The governing plan lives in
[`../windows-win32-shim/CHECKLIST.md`](../windows-win32-shim/CHECKLIST.md).
