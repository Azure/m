// Copyright (c) Microsoft Corporation.

//! IOCP reactor integration smoke test (M7-3.3).
//!
//! Exercises the safe [`windows_threadpool::Io`] reactor over a *real*
//! overlapped file handle: write known bytes to a temp file, reopen it for
//! overlapped I/O, issue an overlapped `ReadFile`, and assert the awaited
//! [`windows_threadpool::Completion`] future resolves with the right result and
//! byte count once the OS completes the operation on a pool thread.

#![cfg(windows)]

use std::future::Future;
use std::os::windows::io::RawHandle;
use std::pin::pin;
use std::sync::{Arc, Condvar, Mutex};
use std::task::{Context, Poll, Wake, Waker};

use windows_sys::Win32::Foundation::{
    CloseHandle, ERROR_IO_PENDING, GENERIC_READ, GetLastError, INVALID_HANDLE_VALUE,
};
use windows_sys::Win32::Storage::FileSystem::{
    CreateFileW, FILE_FLAG_OVERLAPPED, FILE_SHARE_READ, OPEN_EXISTING, ReadFile,
};
use windows_sys::Win32::System::IO::OVERLAPPED;

use windows_threadpool::Io;

const PAYLOAD: &[u8] = b"the quick brown fox jumps over the lazy dog";

/// Minimal park/unpark `block_on` (the executor crate can't be a dev-dependency
/// here without forming a dependency cycle).
fn block_on<F: Future>(future: F) -> F::Output {
    struct Parker {
        notified: Mutex<bool>,
        cv: Condvar,
    }
    impl Wake for Parker {
        fn wake(self: Arc<Self>) {
            self.wake_by_ref();
        }
        fn wake_by_ref(self: &Arc<Self>) {
            *self.notified.lock().unwrap() = true;
            self.cv.notify_one();
        }
    }

    let parker = Arc::new(Parker {
        notified: Mutex::new(false),
        cv: Condvar::new(),
    });
    let waker: Waker = parker.clone().into();
    let mut cx = Context::from_waker(&waker);
    let mut future = pin!(future);
    loop {
        match future.as_mut().poll(&mut cx) {
            Poll::Ready(v) => return v,
            Poll::Pending => {
                let mut notified = parker.notified.lock().unwrap();
                while !*notified {
                    notified = parker.cv.wait(notified).unwrap();
                }
                *notified = false;
            }
        }
    }
}

fn to_wide(path: &std::path::Path) -> Vec<u16> {
    use std::os::windows::ffi::OsStrExt;
    path.as_os_str().encode_wide().chain(std::iter::once(0)).collect()
}

#[test]
fn overlapped_read_completes_via_reactor() {
    // Arrange: a temp file containing a known payload.
    let dir = std::env::temp_dir();
    let path = dir.join(format!("wtp_iocp_{}.bin", std::process::id()));
    std::fs::write(&path, PAYLOAD).expect("write temp file");

    let wide = to_wide(&path);

    // Open the file for overlapped reads.
    // SAFETY: `wide` is a valid NUL-terminated UTF-16 path; the remaining
    // arguments are valid Win32 flag values; the returned handle is checked.
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

    let io = Io::new(handle as RawHandle).expect("bind handle to pool");

    let mut buf = vec![0u8; PAYLOAD.len()];
    let mut overlapped: OVERLAPPED = unsafe { std::mem::zeroed() };

    // Announce, then issue the overlapped read. `buf` and `overlapped` must
    // outlive the operation; the synchronous `block_on` below keeps them alive.
    io.start();
    // SAFETY: `handle` is a live overlapped file handle; `buf` is valid for
    // `PAYLOAD.len()` bytes; `overlapped` is a valid, zeroed OVERLAPPED that
    // outlives the operation; the read count pointer is null as required for
    // overlapped I/O.
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

    // Act: await the completion driven by the thread pool.
    let (io_result, bytes) = block_on(io.completion());

    // Assert.
    assert_eq!(io_result, 0, "completion reported error {io_result}");
    assert_eq!(bytes, PAYLOAD.len());
    assert_eq!(&buf, PAYLOAD);

    // Cleanup.
    io.wait();
    // SAFETY: `handle` is a live handle no longer used by any pending op.
    unsafe { CloseHandle(handle) };
    let _ = std::fs::remove_file(&path);
}
