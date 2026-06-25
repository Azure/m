# merriam

A REST dictionary-store service. `merriam` owns a per-`(locale, user)` **custom
dictionary** on disk and serves it over HTTP; it is the dependent service the
validation tier (windows-win32-shim **SHIM-D23**) carves out of `wordy` so that
`wordy`'s calls to it become the WinHTTP **egress** the shim isolates (MW17).

This directory is a **source-component** (it has this `COMPONENT.md`). It is
layered like `wordy`:

- **`store`** — the on-disk dictionary. Each `(locale, user)` is one
  newline-delimited word-list file read/written through `windows-file-io`
  (native async overlapped Win32 I/O). A *content* store (vs. `wordy`'s
  name-encoded empty files), so it exercises the async overlapped path.
- **dispatch core** (`routes`, MW18-2.2) — a host-agnostic request → response
  router mirroring `wordy`'s custom-dictionary routes 1:1.
- **http.sys listener edge** (MW18-2.3) — the HTTP Server API inbound + a
  server bin + a gated integration test.

Windows-only (its store builds on the Windows-only `windows-file-io`).

See `DESIGN-NOTES.md` for the decisions (`MER-D*`). The driving milestone is
**MW18** in [`../windows-win32-shim/CHECKLIST.md`](../windows-win32-shim/CHECKLIST.md).
