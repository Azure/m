// Copyright (c) Microsoft Corporation.

//! FFI quarantine (TP-D4).
//!
//! This is the **only** module in the crate permitted to use `unsafe`. It binds
//! the raw Win32 thread pool entry points through `windows-sys` (TP-D1) and
//! converts them into safe RAII wrappers and submit APIs consumed by the rest
//! of the crate. The `extern "system"` trampolines that bridge OS callbacks
//! back into boxed Rust closures (TP-D2) live here too.
//!
//! Keeping every `unsafe` line in one small, auditable place is the whole point
//! of the split; nothing above this module needs `unsafe`.

use core::ffi::c_void;
use core::ptr;

use windows_sys::Win32::Foundation::GetLastError;
use windows_sys::Win32::System::Threading::{
    CloseThreadpoolWork, CreateThreadpoolWork, PTP_CALLBACK_INSTANCE, PTP_WORK,
    SubmitThreadpoolWork, WaitForThreadpoolWorkCallbacks,
};

use crate::error::{ThreadPoolError, ThreadPoolResult};

/// The calling thread's last Win32 error code (`GetLastError`).
pub(crate) fn last_error_code() -> u32 {
    // SAFETY: `GetLastError` reads thread-local error state and has no
    // preconditions or side effects.
    unsafe { GetLastError() }
}

/// The persistent callback a [`Work`] runs each time it is submitted.
///
/// `Fn` (not `FnOnce`) because a single work object may be submitted to the
/// pool repeatedly; `Send + Sync` because the callback runs on an arbitrary
/// pool thread and the same object may be submitted from several threads
/// (TP-D2). One-shot ergonomics are layered above this in the safe `work`
/// module by storing a take-once closure behind a mutex.
type WorkCallback = Box<dyn Fn() + Send + Sync + 'static>;

/// RAII wrapper over a `PTP_WORK` thread-pool work object.
///
/// Owns both the OS work handle and the boxed callback it dispatches. On drop
/// it waits for any in-flight callback to finish (so the callback never
/// observes a freed context), closes the handle, then frees the callback box —
/// in that order, which is what makes teardown free of use-after-free (TP-D2).
pub(crate) struct Work {
    handle: PTP_WORK,
    // Thin pointer to the heap-allocated fat `WorkCallback`. Owned by this
    // struct; handed to the OS as the opaque callback context.
    callback: *mut WorkCallback,
}

// SAFETY: `PTP_WORK` submit/wait/close are documented thread-safe, and the
// boxed callback is `Send + Sync` by construction, so a `Work` may be moved to
// and used from any thread.
unsafe impl Send for Work {}
unsafe impl Sync for Work {}

/// The `extern "system"` trampoline the OS invokes on a pool thread.
///
/// Reconstructs a borrow of the boxed callback from the opaque context and runs
/// it. The context is *borrowed*, never freed here: the owning [`Work`] frees
/// it on drop after waiting for callbacks, so reuse across submissions is safe.
unsafe extern "system" fn work_trampoline(
    _instance: PTP_CALLBACK_INSTANCE,
    context: *mut c_void,
    _work: PTP_WORK,
) {
    // SAFETY: `context` is the pointer we passed to `CreateThreadpoolWork`,
    // which points at a live `WorkCallback` owned by the `Work` that scheduled
    // this callback. The `Work` guarantees (via wait-before-free on drop) that
    // the pointee outlives every callback invocation.
    let callback = unsafe { &*(context as *const WorkCallback) };
    callback();
}

impl Work {
    /// Create a work object that runs `callback` each time it is submitted.
    pub(crate) fn new(callback: WorkCallback) -> ThreadPoolResult<Self> {
        let boxed = Box::new(callback);
        let context = Box::into_raw(boxed);

        // SAFETY: `work_trampoline` matches `PTP_WORK_CALLBACK`; `context` is a
        // valid pointer to a live `WorkCallback`; a null environment selects the
        // default pool.
        let handle =
            unsafe { CreateThreadpoolWork(Some(work_trampoline), context.cast(), ptr::null()) };

        if handle == 0 {
            let err = ThreadPoolError::last_os_error();
            // Reclaim the context the OS never took ownership of.
            // SAFETY: `context` came from `Box::into_raw` just above and was not
            // handed to a live OS object (creation failed).
            drop(unsafe { Box::from_raw(context) });
            return Err(err);
        }

        Ok(Self {
            handle,
            callback: context,
        })
    }

    /// Submit the work object to the pool; the callback runs once per submit.
    pub(crate) fn submit(&self) {
        // SAFETY: `self.handle` is a live work object created by `new`.
        unsafe { SubmitThreadpoolWork(self.handle) };
    }

    /// Wait for outstanding callbacks to complete. When `cancel_pending` is
    /// true, submitted-but-not-yet-started callbacks are cancelled instead of
    /// run.
    pub(crate) fn wait_for_callbacks(&self, cancel_pending: bool) {
        // SAFETY: `self.handle` is a live work object created by `new`.
        unsafe { WaitForThreadpoolWorkCallbacks(self.handle, i32::from(cancel_pending)) };
    }
}

impl Drop for Work {
    fn drop(&mut self) {
        // Let any submitted work run to completion before tearing down, so no
        // callback can observe a freed context. (Join-on-drop semantics.)
        self.wait_for_callbacks(false);

        // SAFETY: no callback is running or pending after the wait above, so
        // closing the handle and reclaiming the context race with nothing.
        unsafe { CloseThreadpoolWork(self.handle) };
        drop(unsafe { Box::from_raw(self.callback) });
    }
}
