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
use core::sync::atomic::AtomicU32;

use windows_sys::Win32::Foundation::{FILETIME, GetLastError, HANDLE};
use windows_sys::Win32::System::Threading::{
    CancelThreadpoolIo, CloseThreadpoolIo, CloseThreadpoolTimer, CloseThreadpoolWork,
    CreateThreadpoolIo, CreateThreadpoolTimer, CreateThreadpoolWork, IsThreadpoolTimerSet, PTP_IO,
    PTP_CALLBACK_INSTANCE, PTP_TIMER, PTP_WORK, SetThreadpoolTimer, StartThreadpoolIo,
    SubmitThreadpoolWork, WaitForThreadpoolIoCallbacks, WaitForThreadpoolTimerCallbacks,
    WaitForThreadpoolWorkCallbacks, WaitOnAddress, WakeByAddressSingle,
};

use crate::error::{ThreadPoolError, ThreadPoolResult};

/// The calling thread's last Win32 error code (`GetLastError`).
pub(crate) fn last_error_code() -> u32 {
    // SAFETY: `GetLastError` reads thread-local error state and has no
    // preconditions or side effects.
    unsafe { GetLastError() }
}

/// Whether the OS loader has begun process rundown (`RtlDllShutdownInProgress`).
///
/// ntdll is mapped into every process, so `GetModuleHandleW` never loads it; the
/// export is not in the public SDK headers, so it is resolved once by name and
/// cached. A resolution failure reports `false` — the conservative answer that
/// keeps normal teardown running. Mirrors PIL `process_rundown_in_progress()`.
pub(crate) fn dll_shutdown_in_progress() -> bool {
    use core::sync::atomic::{AtomicUsize, Ordering};
    use windows_sys::Win32::System::LibraryLoader::{GetModuleHandleW, GetProcAddress};

    // 0 = unresolved, 1 = resolved-to-null, else = function pointer (usize).
    static CACHED: AtomicUsize = AtomicUsize::new(0);
    let mut slot = CACHED.load(Ordering::Acquire);
    if slot == 0 {
        // "ntdll.dll\0" as UTF-16.
        const NTDLL: [u16; 10] = [0x6e, 0x74, 0x64, 0x6c, 0x6c, 0x2e, 0x64, 0x6c, 0x6c, 0];
        // SAFETY: a NUL-terminated wide name and a NUL-terminated ASCII export
        // name; both modules/exports may be absent, handled as null.
        let resolved: usize = unsafe {
            let ntdll = GetModuleHandleW(NTDLL.as_ptr());
            if ntdll.is_null() {
                0
            } else {
                GetProcAddress(ntdll, c"RtlDllShutdownInProgress".as_ptr().cast())
                    .map_or(0, |p| p as usize)
            }
        };
        slot = resolved;
        CACHED.store(if slot == 0 { 1 } else { slot }, Ordering::Release);
    }
    if slot <= 1 {
        return false;
    }
    // SAFETY: `slot` is a resolved `RtlDllShutdownInProgress` address; it takes no
    // args and returns BOOLEAN (non-zero ⇒ rundown).
    let f: unsafe extern "system" fn() -> u8 = unsafe { core::mem::transmute(slot) };
    unsafe { f() != 0 }
}

/// `WaitOnAddress` "no timeout" sentinel (Win32 `INFINITE`).
const WAIT_INFINITE: u32 = 0xFFFF_FFFF;

/// Block via `WaitOnAddress` until the `u32` at `flag` differs from `compare`.
/// Spurious returns are possible, so callers re-check the value in a loop.
pub(crate) fn wait_on_address_u32(flag: &AtomicU32, compare: u32) {
    let flag_ptr = (flag as *const AtomicU32).cast::<c_void>();
    let compare_ptr = (&compare as *const u32).cast::<c_void>();
    // SAFETY: `flag` is a live borrowed atom and `compare` a live local; the
    // address size (4) matches a `u32`. `WaitOnAddress` only reads both operands.
    unsafe {
        WaitOnAddress(flag_ptr, compare_ptr, 4, WAIT_INFINITE);
    }
}

/// Wake one thread waiting (via `WaitOnAddress`) on the `u32` at `flag`.
pub(crate) fn wake_by_address_single_u32(flag: &AtomicU32) {
    let flag_ptr = (flag as *const AtomicU32).cast::<c_void>();
    // SAFETY: `flag` is a live borrowed atom; `WakeByAddressSingle` only reads its
    // address to match waiters.
    unsafe {
        WakeByAddressSingle(flag_ptr);
    }
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

/// Run a pool callback so a panic can never unwind across the `extern "system"`
/// FFI boundary — which would abort the process (RS-1).
///
/// A panicking callback is contained and swallowed: the pool thread stays alive
/// and the caller is unaffected, matching the fail-soft contract of consumers
/// like the shim's off-thread journal worker. The callback is wrapped in
/// [`AssertUnwindSafe`](std::panic::AssertUnwindSafe) because on a caught panic
/// we observe no state across the boundary — we simply return.
fn run_contained(callback: impl FnOnce()) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(callback));
}

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
    run_contained(callback);
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

/// The persistent callback a [`Timer`] runs on each expiration.
///
/// `Fn` because a periodic timer fires repeatedly; `Send + Sync` for the same
/// reasons as [`WorkCallback`].
type TimerCallback = Box<dyn Fn() + Send + Sync + 'static>;

/// RAII wrapper over a `PTP_TIMER` thread-pool timer object.
///
/// Owns the OS timer handle and the boxed callback it fires. On drop it cancels
/// the timer, waits for any in-flight callback, closes the handle, then frees
/// the callback box — in that order, so a firing callback never observes a
/// freed context.
pub(crate) struct Timer {
    handle: PTP_TIMER,
    callback: *mut TimerCallback,
}

// SAFETY: `PTP_TIMER` set/wait/close are documented thread-safe, and the boxed
// callback is `Send + Sync` by construction.
unsafe impl Send for Timer {}
unsafe impl Sync for Timer {}

/// The `extern "system"` trampoline the OS invokes when the timer expires.
unsafe extern "system" fn timer_trampoline(
    _instance: PTP_CALLBACK_INSTANCE,
    context: *mut c_void,
    _timer: PTP_TIMER,
) {
    // SAFETY: `context` points at a live `TimerCallback` owned by the `Timer`
    // that armed this expiration; the `Timer` keeps it alive until after it has
    // cancelled and waited for callbacks on drop.
    let callback = unsafe { &*(context as *const TimerCallback) };
    run_contained(callback);
}

impl Timer {
    /// Create a timer that runs `callback` on each expiration. The timer is
    /// created unset; call [`Timer::set`] to arm it.
    pub(crate) fn new(callback: TimerCallback) -> ThreadPoolResult<Self> {
        let context = Box::into_raw(Box::new(callback));

        // SAFETY: `timer_trampoline` matches `PTP_TIMER_CALLBACK`; `context` is
        // a live `TimerCallback`; a null environment selects the default pool.
        let handle =
            unsafe { CreateThreadpoolTimer(Some(timer_trampoline), context.cast(), ptr::null()) };

        if handle == 0 {
            let err = ThreadPoolError::last_os_error();
            // SAFETY: `context` came from `Box::into_raw` and was not taken by a
            // live OS object (creation failed).
            drop(unsafe { Box::from_raw(context) });
            return Err(err);
        }

        Ok(Self {
            handle,
            callback: context,
        })
    }

    /// Arm the timer. `due_time` is a relative FILETIME (negative 100-ns units)
    /// for the first expiration; `period_ms` is the repeat period (0 = one-shot)
    /// and `window_ms` is the coalescing window.
    pub(crate) fn set(&self, due_time: FILETIME, period_ms: u32, window_ms: u32) {
        // SAFETY: `self.handle` is a live timer; `&due_time` is a valid pointer
        // for the duration of the call.
        unsafe { SetThreadpoolTimer(self.handle, &due_time, period_ms, window_ms) };
    }

    /// Cancel a pending expiration (the timer object stays reusable).
    pub(crate) fn cancel(&self) {
        // SAFETY: `self.handle` is a live timer; a null due-time cancels.
        unsafe { SetThreadpoolTimer(self.handle, ptr::null(), 0, 0) };
    }

    /// Whether the timer currently has a pending expiration.
    pub(crate) fn is_set(&self) -> bool {
        // SAFETY: `self.handle` is a live timer.
        unsafe { IsThreadpoolTimerSet(self.handle) != 0 }
    }

    /// Wait for outstanding expiration callbacks to finish.
    pub(crate) fn wait_for_callbacks(&self, cancel_pending: bool) {
        // SAFETY: `self.handle` is a live timer.
        unsafe { WaitForThreadpoolTimerCallbacks(self.handle, i32::from(cancel_pending)) };
    }
}

impl Drop for Timer {
    fn drop(&mut self) {
        self.cancel();
        self.wait_for_callbacks(true);

        // SAFETY: cancelled and drained above, so close and free race with
        // nothing.
        unsafe { CloseThreadpoolTimer(self.handle) };
        drop(unsafe { Box::from_raw(self.callback) });
    }
}

/// The callback a [`Io`] runs on each overlapped-I/O completion.
///
/// Receives the completion's `io_result` (a Win32 error code; `0` is success)
/// and the number of bytes transferred. `Fn` because one bound handle may
/// complete many operations; `Send + Sync` because completions run on arbitrary
/// pool threads (TP-D2).
type IoCallback = Box<dyn Fn(u32, usize) + Send + Sync + 'static>;

/// RAII wrapper over a `PTP_IO` thread-pool I/O completion object.
///
/// Binds a `HANDLE` to the pool's completion port. Each overlapped operation is
/// announced with [`Io::start`] before it is issued; when the OS completes it,
/// the pool invokes the bound callback. On drop it waits for in-flight
/// completions, closes the handle, then frees the callback box — in that order,
/// so a completion never observes a freed context.
pub(crate) struct Io {
    handle: PTP_IO,
    callback: *mut IoCallback,
}

// SAFETY: `PTP_IO` start/cancel/wait/close are documented thread-safe, and the
// boxed callback is `Send + Sync` by construction.
unsafe impl Send for Io {}
unsafe impl Sync for Io {}

/// The `extern "system"` trampoline the OS invokes on a pool thread when an
/// overlapped operation on the bound handle completes.
unsafe extern "system" fn io_trampoline(
    _instance: PTP_CALLBACK_INSTANCE,
    context: *mut c_void,
    _overlapped: *mut c_void,
    io_result: u32,
    bytes_transferred: usize,
    _io: PTP_IO,
) {
    // SAFETY: `context` points at a live `IoCallback` owned by the `Io` that
    // bound this handle; the `Io` keeps it alive until after it has waited for
    // callbacks on drop.
    let callback = unsafe { &*(context as *const IoCallback) };
    run_contained(|| callback(io_result, bytes_transferred));
}

impl Io {
    /// Bind `handle` to the pool, dispatching completions to `callback`.
    pub(crate) fn new(handle: HANDLE, callback: IoCallback) -> ThreadPoolResult<Self> {
        let context = Box::into_raw(Box::new(callback));

        // SAFETY: `io_trampoline` matches `PTP_WIN32_IO_CALLBACK`; `context` is
        // a live `IoCallback`; a null environment selects the default pool.
        let io =
            unsafe { CreateThreadpoolIo(handle, Some(io_trampoline), context.cast(), ptr::null()) };

        if io == 0 {
            let err = ThreadPoolError::last_os_error();
            // SAFETY: `context` came from `Box::into_raw` and was not taken by a
            // live OS object (creation failed).
            drop(unsafe { Box::from_raw(context) });
            return Err(err);
        }

        Ok(Self {
            handle: io,
            callback: context,
        })
    }

    /// Announce that an overlapped operation is about to be issued on the bound
    /// handle. Must be called before each operation; pair with [`Io::cancel`]
    /// if issuing the operation fails synchronously.
    pub(crate) fn start(&self) {
        // SAFETY: `self.handle` is a live I/O object created by `new`.
        unsafe { StartThreadpoolIo(self.handle) };
    }

    /// Cancel a [`Io::start`] announcement when the operation was not actually
    /// issued (e.g. the overlapped call failed without pending).
    pub(crate) fn cancel(&self) {
        // SAFETY: `self.handle` is a live I/O object created by `new`.
        unsafe { CancelThreadpoolIo(self.handle) };
    }

    /// Wait for outstanding completion callbacks to finish.
    pub(crate) fn wait_for_callbacks(&self, cancel_pending: bool) {
        // SAFETY: `self.handle` is a live I/O object created by `new`.
        unsafe { WaitForThreadpoolIoCallbacks(self.handle, i32::from(cancel_pending)) };
    }
}

impl Drop for Io {
    fn drop(&mut self) {
        self.wait_for_callbacks(false);

        // SAFETY: drained above, so close and free race with nothing.
        unsafe { CloseThreadpoolIo(self.handle) };
        drop(unsafe { Box::from_raw(self.callback) });
    }
}
