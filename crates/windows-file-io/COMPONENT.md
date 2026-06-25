# windows-file-io

Native async Win32 file I/O: overlapped `CreateFile` / `ReadFile` / `WriteFile`
with completion delivered by the Windows thread pool.

This directory is a **source-component** (it has this `COMPONENT.md`). It
comprises two crates:

- **`windows-file-io`** — the safe async surface (`File`), `#![forbid(unsafe_code)]`.
- **`windows-file-io-sys`** — the `unsafe` FFI leaf (RAII overlapped handle +
  raw overlapped-issue primitives). Mirrors `windows-platform-isolation-sys`.

The crate is **async-first even though small operations usually complete
synchronously** (owner directive): the handle is bound to the thread pool
without `FILE_SKIP_COMPLETION_PORT_ON_SUCCESS`, so every operation posts a
completion packet and is awaited. It takes no dependency on an async runtime;
drive the futures with any executor (e.g. `windows-threadpool-executor`).

Built on the `windows-threadpool` IOCP reactor (`Io` / `Completion`). Consumed
by `merriam` (the dictionary-store service) for its on-disk word files.

See `DESIGN-NOTES.md` for the design decisions (`D-FIO-*`). The milestone plan
that produced this component is **MW18** in
[`../windows-win32-shim/CHECKLIST.md`](../windows-win32-shim/CHECKLIST.md).
