// Copyright (c) Microsoft Corporation.

//! A futures executor layered on the Windows thread pool.
//!
//! Idle async tasks tie up no dedicated threads: the executor's `async-task`
//! schedule closure submits a [`windows_threadpool`] work item, so a task only
//! occupies a pool thread while it is actually being polled (TP-D3).
//!
//! Two entry points are planned:
//! - `Executor::spawn` schedules a future onto the thread pool and returns a
//!   join handle for its result.
//! - `block_on` drives a future to completion on the calling thread using a
//!   park/unpark waker, independent of the pool.
//!
//! This crate contains no `unsafe`; all of the Win32 `unsafe` lives in the
//! `windows-threadpool` `ffi` module (TP-D4). It is Windows-only; on other
//! platforms it compiles to nothing.

#![forbid(unsafe_code)]

#[cfg(windows)]
extern crate windows_threadpool as _;
