// Copyright (c) Microsoft Corporation.

//! Unsafe FFI leaf for the `windows-file-io` async file crate.
//!
//! This is the **`unsafe` leaf** (mirroring `windows-platform-isolation-sys`):
//! it wraps the overlapped-I/O-critical Win32 file entry points as **safe**,
//! slice-in / owned-out primitives over a RAII [`FileHandle`]. The file is
//! always opened with `FILE_FLAG_OVERLAPPED` so a higher layer can bind it to
//! the `windows-threadpool` IOCP reactor and drive asynchronous completion; the
//! handle is **exposed** via [`AsRawHandle`](std::os::windows::io::AsRawHandle)
//! for exactly that purpose.
//!
//! The asynchronous contract is owned here: [`OverlappedOp::issue_read`] /
//! [`issue_write`](OverlappedOp::issue_write) start one overlapped operation and
//! report whether the caller must await a thread-pool completion
//! ([`Issue::Pending`]), whether the operation already reached end-of-file with
//! no completion to wait for ([`Issue::Eof`]), or whether it failed
//! synchronously ([`Issue::Failed`]). Because the handle is bound to the pool
//! (no `FILE_SKIP_COMPLETION_PORT_ON_SUCCESS`), even a *synchronous* success
//! posts a completion packet — so a successful issue is always reported as
//! [`Issue::Pending`], realizing the "async-first even when it completes
//! synchronously" directive.
//!
//! No raw pointers and no handle lifetimes cross this crate's boundary; every
//! `CloseHandle`, `OVERLAPPED` offset, and `WIN32_ERROR` capture is confined
//! here. The safe `windows-file-io` crate consumes these primitives while
//! itself remaining `#![forbid(unsafe_code)]`.
//!
//! On non-Windows targets this crate compiles to an empty library, mirroring
//! `windows-platform-isolation-sys`.

#[cfg(windows)]
mod file;

#[cfg(windows)]
pub use file::{FileHandle, Issue, OpenMode, OverlappedOp, file_size, open, set_end_of_file};
