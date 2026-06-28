// Copyright (c) Microsoft Corporation.

//! Safe work-submission API over the Windows thread pool.
//!
//! [`Work`] is a reusable work object: each [`Work::submit`] schedules one run
//! of its callback on a pool thread. [`submit_once`] is the one-shot
//! convenience — it boxes a `FnOnce`, submits it a single time, and hands back
//! the owning [`Work`] so the caller can join (or simply drop to join).
//!
//! Callbacks run on arbitrary pool threads, so all submitted closures are
//! `Send + 'static` (TP-D2). No dedicated thread is consumed while work is idle.

use std::sync::Mutex;

use crate::error::ThreadPoolResult;
use crate::ffi;

/// A reusable thread-pool work object.
///
/// Dropping a `Work` waits for any in-flight callback to finish before
/// releasing the OS handle and the callback, so a callback can never observe a
/// freed context (join-on-drop).
pub struct Work {
    inner: ffi::Work,
}

impl Work {
    /// Create a work object that runs `callback` each time it is submitted.
    ///
    /// The callback is `Fn` because the object may be submitted repeatedly (the
    /// shape the async executor needs — its schedule closure re-submits the same
    /// work item every time a task is woken, TP-D3).
    pub fn new<F>(callback: F) -> ThreadPoolResult<Self>
    where
        F: Fn() + Send + Sync + 'static,
    {
        Ok(Self {
            inner: ffi::Work::new(Box::new(callback))?,
        })
    }

    /// Schedule one run of the callback on a pool thread.
    pub fn submit(&self) {
        self.inner.submit();
    }

    /// Wait for all outstanding callbacks to finish running.
    pub fn wait(&self) {
        self.inner.wait_for_callbacks(false);
    }

    /// Cancel callbacks that have been submitted but not yet started, and wait
    /// for any already-running callback to finish.
    pub fn cancel_pending(&self) {
        self.inner.wait_for_callbacks(true);
    }
}

/// Submit a one-shot `FnOnce` to the pool and return the owning [`Work`].
///
/// The closure runs exactly once. Drop (or [`Work::wait`]) the returned handle
/// to join. The `FnOnce` is stored behind a take-once mutex so it can be driven
/// by the reusable `Fn`-shaped primitive without requiring the closure itself
/// to be callable more than once.
pub fn submit_once<F>(f: F) -> ThreadPoolResult<Work>
where
    F: FnOnce() + Send + 'static,
{
    let slot = Mutex::new(Some(f));
    let work = Work::new(move || {
        // `take()` makes this a one-shot even though the primitive is `Fn`.
        let taken = slot
            .lock()
            .expect("work callback mutex poisoned")
            .take();
        if let Some(f) = taken {
            f();
        }
    })?;
    work.submit();
    Ok(work)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Arc;
    use std::sync::atomic::{AtomicU32, Ordering};

    #[test]
    fn submit_once_runs_exactly_once_and_joins() {
        let counter = Arc::new(AtomicU32::new(0));
        let c = Arc::clone(&counter);
        let work = submit_once(move || {
            c.fetch_add(1, Ordering::SeqCst);
        })
        .expect("submit");
        work.wait();
        assert_eq!(counter.load(Ordering::SeqCst), 1);
        // Dropping is a redundant join; must not run the closure again.
        drop(work);
        assert_eq!(counter.load(Ordering::SeqCst), 1);
    }

    #[test]
    fn reusable_work_runs_per_submit() {
        let counter = Arc::new(AtomicU32::new(0));
        let c = Arc::clone(&counter);
        let work = Work::new(move || {
            c.fetch_add(1, Ordering::SeqCst);
        })
        .expect("create");
        for _ in 0..5 {
            work.submit();
        }
        work.wait();
        assert_eq!(counter.load(Ordering::SeqCst), 5);
    }

    #[test]
    fn join_on_drop_waits_for_completion() {
        let counter = Arc::new(AtomicU32::new(0));
        let c = Arc::clone(&counter);
        {
            let _work = submit_once(move || {
                c.fetch_add(1, Ordering::SeqCst);
            })
            .expect("submit");
            // No explicit wait: drop must join.
        }
        assert_eq!(counter.load(Ordering::SeqCst), 1);
    }

    #[test]
    fn panicking_callback_is_contained_and_pool_survives() {
        // A panic in a pool callback must not unwind across the `extern "system"`
        // trampoline (which would abort the process); the trampoline contains it
        // (RS-1). The panic message is printed by the default hook — that stderr
        // noise is expected. The pool stays usable afterward.
        let work = submit_once(|| panic!("boom in a pool callback")).expect("submit");
        work.wait(); // must return, not abort

        let ran = Arc::new(AtomicU32::new(0));
        let r = Arc::clone(&ran);
        let work2 = submit_once(move || {
            r.fetch_add(1, Ordering::SeqCst);
        })
        .expect("submit");
        work2.wait();
        assert_eq!(ran.load(Ordering::SeqCst), 1);
    }
}
