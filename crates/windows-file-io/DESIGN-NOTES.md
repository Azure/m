# windows-file-io — Design Notes

Current canonical decisions for the `windows-file-io` component (the safe
`windows-file-io` crate + its `windows-file-io-sys` unsafe leaf). Produced by
**MW18-1** (validation tier, see `../windows-win32-shim/DESIGN-NOTES.md` SHIM-D23).

## D-FIO-1 — Two-crate split: safe surface over an `unsafe` leaf

`windows-file-io-sys` owns **every** `unsafe`: the RAII overlapped `FileHandle`,
`CreateFileW`/`ReadFile`/`WriteFile`/`SetEndOfFile`/`GetFileSizeEx` calls, and
the `OVERLAPPED` offset writes. `windows-file-io` is `#![forbid(unsafe_code)]`
and builds the `File` future surface on top, plus the `windows-threadpool` IOCP
reactor (whose own `unsafe` is in `windows-threadpool`'s `ffi`). This mirrors
`windows-platform-isolation` / `-sys` exactly. No raw pointer or handle lifetime
crosses the leaf boundary.

## D-FIO-2 — Async-first even when completion is synchronous

The owner's directive: write the code as if completion is always deferred. The
file handle is bound to the pool via `CreateThreadpoolIo` **without**
`SetFileCompletionNotificationModes(FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)`, so a
synchronous `ReadFile`/`WriteFile` success **still** posts a completion packet.
Therefore `OverlappedOp::issue_{read,write}` reports a successful issue as
`Issue::Pending` (await the pool completion) regardless of whether the OS
finished synchronously. The synchronous fast path is *handled* (it works) but
never *assumed* — there is one code path: issue, then await.

## D-FIO-3 — `OVERLAPPED` is boxed; the buffer and op outlive the await

An overlapped operation hands the kernel two pointers — the data buffer and the
`OVERLAPPED` — that must remain valid and unmoved until completion. The leaf
boxes the `OVERLAPPED` (`OverlappedOp`) for a stable heap address; the safe
`read_at`/`write_at` hold both the `OverlappedOp` and the caller's `&[u8]` /
`&mut [u8]` across the `.await`, so neither moves nor drops while in flight. The
soundness obligation (the kernel writes the buffer during the await, invisibly
to the borrow checker) is the documented leaf contract, identical in shape to
the `windows-threadpool` `iocp.rs` reference.

## D-FIO-4 — One operation in flight per `File`; reactor bound once

`Io::new` binds the handle to the pool exactly once at open. The reactor models
a single in-flight operation, so `read_at`/`write_at` take `&mut self`, making
"one op at a time per `File`" a compile-time guarantee rather than a runtime
hazard. Higher layers wanting concurrency open multiple `File`s.

## D-FIO-5 — Owned `FileError`; no `windows-sys` in the safe layer (Design Autonomy)

The safe crate exposes `FileError(u32)` (a `WIN32_ERROR` code) with `code()` /
`is_not_found()`, and interprets the handful of codes it needs
(`FILE_NOT_FOUND`, `PATH_NOT_FOUND`, `HANDLE_EOF`, `WRITE_FAULT`) as **named
constants it owns** (changing a value is a breaking change). The `windows-sys`
binding stays an implementation detail of the leaf — the safe surface does not
depend on it.

## D-FIO-6 — End-of-file is a synchronous non-completion

An overlapped read at/after EOF fails synchronously with `ERROR_HANDLE_EOF` and
posts **no** completion packet. `issue_read` therefore returns a distinct
`Issue::Eof`, and `read_at` reports it as `Ok(0)` after `Io::cancel()` (to
balance the `Io::start()` announcement) — it must not await a completion that
will never arrive. A read that *partially* fills then hits EOF completes
normally with the short byte count.
