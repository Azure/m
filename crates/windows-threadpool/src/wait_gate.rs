// Copyright (c) Microsoft Corporation.

//! A one-shot cross-thread completion latch over `WaitOnAddress`.
//!
//! [`WaitGate`] is the synchronous-handoff primitive a caller uses to block until
//! a thread-pool work item (or any other holder of a clone) reports it is done.
//! It is backed by `WaitOnAddress` / `WakeByAddressSingle`: no kernel object is
//! allocated while uncontended, and the wait is a futex-style spin on a shared
//! `u32`. The `unsafe` for the two intrinsics lives in [`crate::ffi`] (TP-D4); this
//! module is safe Rust.

use std::sync::Arc;
use std::sync::atomic::{AtomicU32, Ordering};

use crate::ffi;

/// The latch has not been signaled yet.
const UNSIGNALED: u32 = 0;
/// The latch has been signaled.
const SIGNALED: u32 = 1;

/// A one-shot completion latch backed by `WaitOnAddress` / `WakeByAddressSingle`.
///
/// A holder calls [`signal`](WaitGate::signal) when its work is done; a waiter
/// blocks in [`wait`](WaitGate::wait) until then. Clones share one latch (an
/// `Arc`-backed atom), so the waiter and the signaler can hold independent handles
/// — e.g. one moved into a thread-pool work item, one kept on the calling thread.
#[derive(Clone, Debug)]
pub struct WaitGate {
    flag: Arc<AtomicU32>,
}

impl WaitGate {
    /// A fresh, unsignaled latch.
    #[must_use]
    pub fn new() -> Self {
        Self {
            flag: Arc::new(AtomicU32::new(UNSIGNALED)),
        }
    }

    /// Mark the latch signaled and wake a waiter, if any. Idempotent.
    pub fn signal(&self) {
        self.flag.store(SIGNALED, Ordering::Release);
        ffi::wake_by_address_single_u32(self.flag.as_ref());
    }

    /// Block until the latch is signaled. Returns immediately if already signaled.
    pub fn wait(&self) {
        // Re-check after every (possibly spurious) wake. A `signal` between the
        // load and the `WaitOnAddress` makes the compared value already differ, so
        // the wait returns at once — no lost wakeup.
        while self.flag.load(Ordering::Acquire) == UNSIGNALED {
            ffi::wait_on_address_u32(self.flag.as_ref(), UNSIGNALED);
        }
    }

    /// Whether the latch has been signaled.
    #[must_use]
    pub fn is_signaled(&self) -> bool {
        self.flag.load(Ordering::Acquire) == SIGNALED
    }
}

impl Default for WaitGate {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::submit_once;

    #[test]
    fn wait_returns_once_signaled_from_a_pool_thread() {
        let gate = WaitGate::new();
        let signaler = gate.clone();
        let work = submit_once(move || {
            signaler.signal();
        })
        .expect("submit");
        gate.wait(); // blocks until the pool work item signals
        assert!(gate.is_signaled());
        work.wait();
    }

    #[test]
    fn wait_on_an_already_signaled_gate_does_not_block() {
        let gate = WaitGate::new();
        gate.signal();
        gate.wait();
        assert!(gate.is_signaled());
    }

    #[test]
    fn a_fresh_gate_is_unsignaled() {
        assert!(!WaitGate::new().is_signaled());
        assert!(!WaitGate::default().is_signaled());
    }
}
