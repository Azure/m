// Copyright (c) Microsoft Corporation.

//! Native async Win32 file I/O.
//!
//! [`File`] performs overlapped `CreateFile` / `ReadFile` / `WriteFile` with
//! completion delivered by the Windows thread pool — it binds the file handle
//! to the [`windows_threadpool`] IOCP reactor and turns each operation into a
//! `Future`. The API is **async-first even though small operations usually
//! complete synchronously** (owner's directive): the handle is bound to the
//! pool without `FILE_SKIP_COMPLETION_PORT_ON_SUCCESS`, so even a synchronous
//! success posts a completion packet and is awaited like any other — the code
//! never assumes the synchronous fast path.
//!
//! This crate is **safe** (`#![forbid(unsafe_code)]`): every `unsafe` lives in
//! the [`windows_file_io_sys`] leaf and the [`windows_threadpool`] reactor. It
//! takes no dependency on an async runtime; drive the returned futures with any
//! executor (e.g. [`windows_threadpool_executor::block_on`]).
//!
//! On non-Windows targets this crate compiles to an empty library.

#![forbid(unsafe_code)]

#[cfg(windows)]
mod file;

#[cfg(windows)]
pub use file::{File, FileError, FileResult};
