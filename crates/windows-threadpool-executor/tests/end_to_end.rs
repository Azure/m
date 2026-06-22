// Copyright (c) Microsoft Corporation.

//! Cross-crate end-to-end async smoke test (M7-4).
//!
//! Drives the whole async foundation in a single scenario: an [`Executor`]
//! spawned task, a thread-pool [`Timer`] surfaced as an awaitable, and an
//! [`Io`] IOCP reactor completing a real overlapped `ReadFile` — all awaited
//! together under one [`block_on`].

#![cfg(windows)]

use std::future::Future;
use std::os::windows::io::RawHandle;
use std::pin::Pin;
use std::sync::{Arc, Mutex};
use std::task::{Context, Poll, Waker};
use std::time::Duration;

use windows_sys::Win32::Foundation::{
    CloseHandle, ERROR_IO_PENDING, GENERIC_READ, GetLastError, INVALID_HANDLE_VALUE,
};
use windows_sys::Win32::Storage::FileSystem::{
    CreateFileW, FILE_FLAG_OVERLAPPED, FILE_SHARE_READ, OPEN_EXISTING, ReadFile,
};
use windows_sys::Win32::System::IO::OVERLAPPED;

use windows_threadpool::{Io, Timer};
use windows_threadpool_executor::{Executor, block_on};

const PAYLOAD: &[u8] = b"end-to-end overlapped completion through the pool";

/// A one-shot signal driven from a thread-pool callback, awaitable as a future.
#[derive(Clone)]
struct Signal(Arc<Mutex<(bool, Option<Waker>)>>);

impl Signal {
    fn new() -> Self {
        Self(Arc::new(Mutex::new((false, None))))
    }

    /// Fire the signal and wake any awaiting task (called from a pool thread).
    fn fire(&self) {
        let waker = {
            let mut state = self.0.lock().unwrap();
            state.0 = true;
            state.1.take()
        };
        if let Some(waker) = waker {
            waker.wake();
        }
    }

    /// A future that resolves once [`Signal::fire`] has been called.
    fn wait(&self) -> SignalFuture {
        SignalFuture(self.0.clone())
    }
}

struct SignalFuture(Arc<Mutex<(bool, Option<Waker>)>>);

impl Future for SignalFuture {
    type Output = ();

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<()> {
        let mut state = self.0.lock().unwrap();
        if state.0 {
            Poll::Ready(())
        } else {
            state.1 = Some(cx.waker().clone());
            Poll::Pending
        }
    }
}

fn to_wide(path: &std::path::Path) -> Vec<u16> {
    use std::os::windows::ffi::OsStrExt;
    path.as_os_str().encode_wide().chain(std::iter::once(0)).collect()
}

#[test]
fn executor_timer_and_iocp_compose_under_block_on() {
    // Arrange a temp file with a known payload, opened for overlapped reads.
    let path = std::env::temp_dir().join(format!("wtpe_e2e_{}.bin", std::process::id()));
    std::fs::write(&path, PAYLOAD).expect("write temp file");
    let wide = to_wide(&path);

    // SAFETY: `wide` is a valid NUL-terminated UTF-16 path; flag values are
    // valid; the returned handle is checked below.
    let handle = unsafe {
        CreateFileW(
            wide.as_ptr(),
            GENERIC_READ,
            FILE_SHARE_READ,
            std::ptr::null(),
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            std::ptr::null_mut(),
        )
    };
    assert!(
        !std::ptr::eq(handle, INVALID_HANDLE_VALUE),
        "CreateFileW failed"
    );

    let executor = Executor::new().expect("create executor");
    let io = Io::new(handle as RawHandle).expect("bind handle to pool");

    let signal = Signal::new();
    let timer_signal = signal.clone();
    let timer = Timer::new(move || timer_signal.fire()).expect("create timer");

    let mut buf = vec![0u8; PAYLOAD.len()];
    let mut overlapped: OVERLAPPED = unsafe { std::mem::zeroed() };

    let (io_result, bytes, spawned, timer_fired) = block_on(async {
        // spawn + await
        let task = executor.spawn(async { 6 * 7 });

        // arm the timer (fires the signal on a pool thread)
        timer.schedule(Duration::from_millis(5));

        // issue the overlapped read, announced to the pool first
        io.start();
        // SAFETY: `handle` is a live overlapped handle; `buf` is valid for
        // `PAYLOAD.len()` bytes; `overlapped` outlives the operation; the read
        // count pointer is null as required for overlapped I/O.
        let ok = unsafe {
            ReadFile(
                handle,
                buf.as_mut_ptr(),
                PAYLOAD.len() as u32,
                std::ptr::null_mut(),
                &mut overlapped,
            )
        };
        if ok == 0 {
            // SAFETY: no preconditions.
            let err = unsafe { GetLastError() };
            if err != ERROR_IO_PENDING {
                io.cancel();
                panic!("ReadFile failed synchronously: {err}");
            }
        }

        // await all three: IOCP completion, timer fire, spawned task
        let (io_result, bytes) = io.completion().await;
        signal.wait().await;
        let spawned = task.await;
        (io_result, bytes, spawned, true)
    });

    assert_eq!(io_result, 0, "completion reported error {io_result}");
    assert_eq!(bytes, PAYLOAD.len());
    assert_eq!(&buf, PAYLOAD);
    assert_eq!(spawned, 42);
    assert!(timer_fired);

    io.wait();
    timer.wait();
    // SAFETY: `handle` is live and no longer used by any pending operation.
    unsafe { CloseHandle(handle) };
    let _ = std::fs::remove_file(&path);
}
