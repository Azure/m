// Copyright (c) Microsoft Corporation.

//! Safe IOCP reactor over the thread pool's I/O completion object (TP-D3).
//!
//! [`Io`] binds a handle to the pool and turns a single in-flight overlapped
//! operation into an awaitable [`Completion`] future: the OS completes the
//! operation on a pool thread, which wakes the awaiting task — no dedicated
//! reactor thread.

use std::future::Future;
use std::os::windows::io::RawHandle;
use std::pin::Pin;
use std::sync::{Arc, Mutex};
use std::task::{Context, Poll, Waker};

use crate::error::ThreadPoolResult;
use crate::ffi;

/// Completion state for the single in-flight operation bound to an [`Io`].
#[derive(Default)]
struct Inner {
    /// `Some((io_result, bytes))` once the operation has completed.
    result: Option<(u32, usize)>,
    /// Waker registered by a pending [`Completion`] poll.
    waker: Option<Waker>,
}

/// Shared between the reactor and its completion callback.
#[derive(Default)]
struct Shared {
    inner: Mutex<Inner>,
}

impl Shared {
    /// Record a completion and wake any awaiting task.
    fn complete(&self, io_result: u32, bytes: usize) {
        let waker = {
            let mut inner = self.inner.lock().unwrap();
            inner.result = Some((io_result, bytes));
            inner.waker.take()
        };
        if let Some(waker) = waker {
            waker.wake();
        }
    }

    /// Clear state ahead of a new operation.
    fn reset(&self) {
        let mut inner = self.inner.lock().unwrap();
        inner.result = None;
        inner.waker = None;
    }
}

/// A thread-pool I/O completion object bound to a handle.
///
/// One overlapped operation may be in flight at a time: call [`Io::start`]
/// before issuing the operation, then await [`Io::completion`]. If issuing the
/// operation fails synchronously (without leaving it pending), call
/// [`Io::cancel`] to balance the [`Io::start`] announcement.
pub struct Io {
    inner: ffi::Io,
    shared: Arc<Shared>,
}

impl Io {
    /// Bind `handle` (which must be opened for overlapped I/O) to the pool.
    pub fn new(handle: RawHandle) -> ThreadPoolResult<Self> {
        let shared = Arc::new(Shared::default());
        let cb = Arc::clone(&shared);
        let inner = ffi::Io::new(handle, Box::new(move |io_result, bytes| cb.complete(io_result, bytes)))?;
        Ok(Self { inner, shared })
    }

    /// Announce that an overlapped operation is about to be issued. Resets the
    /// completion state for the new operation.
    pub fn start(&self) {
        self.shared.reset();
        self.inner.start();
    }

    /// Cancel a [`Io::start`] announcement when the operation was not issued.
    pub fn cancel(&self) {
        self.inner.cancel();
    }

    /// Await the completion of the started operation, resolving to
    /// `(io_result, bytes_transferred)` where `io_result` is a Win32 error code
    /// (`0` is success).
    pub fn completion(&self) -> Completion {
        Completion {
            shared: Arc::clone(&self.shared),
        }
    }

    /// Block until any outstanding completion callback has finished running.
    pub fn wait(&self) {
        self.inner.wait_for_callbacks(false);
    }
}

/// A future resolving when the [`Io`]'s started operation completes.
pub struct Completion {
    shared: Arc<Shared>,
}

impl Future for Completion {
    type Output = (u32, usize);

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<(u32, usize)> {
        let mut inner = self.shared.inner.lock().unwrap();
        if let Some(result) = inner.result {
            Poll::Ready(result)
        } else {
            inner.waker = Some(cx.waker().clone());
            Poll::Pending
        }
    }
}
