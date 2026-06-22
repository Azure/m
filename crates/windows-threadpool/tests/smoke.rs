// Copyright (c) Microsoft Corporation.

//! Integration smoke tests for the public `windows-threadpool` surface.
//!
//! These exercise the crate end-to-end through its public API at a larger scale
//! than the in-module unit tests: submit-and-join, timers, sequenced vs
//! parallel ordering, cancellation, and queue drain.

#![cfg(windows)]

use std::sync::Arc;
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::{Condvar, Mutex};
use std::time::Duration;

use windows_threadpool::{
    ExecutionPolicy, PeriodicTimer, Timer, WorkItemState, WorkQueue, submit_once,
};

const POLL_BUDGET: Duration = Duration::from_secs(10);

#[test]
fn submit_once_many_all_run_and_join() {
    const N: u32 = 1_000;
    let counter = Arc::new(AtomicU32::new(0));
    let mut handles = Vec::with_capacity(N as usize);
    for _ in 0..N {
        let c = Arc::clone(&counter);
        handles.push(submit_once(move || {
            c.fetch_add(1, Ordering::SeqCst);
        }));
    }
    for h in handles {
        h.expect("submit").wait();
    }
    assert_eq!(counter.load(Ordering::SeqCst), N);
}

#[test]
fn parallel_queue_runs_thousands() {
    const N: u32 = 2_000;
    let queue = WorkQueue::new(ExecutionPolicy::Parallel).expect("create");
    let counter = Arc::new(AtomicU32::new(0));
    let mut items = Vec::with_capacity(N as usize);
    for _ in 0..N {
        let c = Arc::clone(&counter);
        items.push(queue.enqueue(move || {
            c.fetch_add(1, Ordering::SeqCst);
        }));
    }
    assert!(queue.wait_for(POLL_BUDGET), "queue did not drain");
    for item in &items {
        item.wait();
        assert_eq!(item.state(), WorkItemState::Done);
    }
    assert_eq!(counter.load(Ordering::SeqCst), N);
}

#[test]
fn sequenced_queue_preserves_order_at_scale() {
    const N: u32 = 500;
    let queue = WorkQueue::new(ExecutionPolicy::Sequenced).expect("create");
    let log = Arc::new(Mutex::new(Vec::with_capacity(N as usize)));
    for i in 0..N {
        let l = Arc::clone(&log);
        queue.enqueue(move || l.lock().unwrap().push(i));
    }
    assert!(queue.wait_for(POLL_BUDGET), "queue did not drain");
    let observed = log.lock().unwrap().clone();
    let expected: Vec<u32> = (0..N).collect();
    assert_eq!(observed, expected);
}

#[test]
fn one_shot_timer_fires() {
    let fired = Arc::new(AtomicU32::new(0));
    let f = Arc::clone(&fired);
    let timer = Timer::new(move || {
        f.fetch_add(1, Ordering::SeqCst);
    })
    .expect("create");
    timer.schedule(Duration::from_millis(5));
    poll_until(|| fired.load(Ordering::SeqCst) == 1);
    timer.wait();
    assert_eq!(fired.load(Ordering::SeqCst), 1);
}

#[test]
fn periodic_timer_n_fires_then_cancel() {
    let fired = Arc::new(AtomicU32::new(0));
    let f = Arc::clone(&fired);
    let timer = PeriodicTimer::new(move || {
        f.fetch_add(1, Ordering::SeqCst);
    })
    .expect("create");
    timer.schedule(Duration::from_millis(2), Duration::from_millis(2));
    poll_until(|| fired.load(Ordering::SeqCst) >= 5);
    timer.cancel();
    timer.wait();
    let count = fired.load(Ordering::SeqCst);
    assert!(count >= 5, "expected >= 5 fires, got {count}");
    // After cancel + wait, no further fires.
    std::thread::sleep(Duration::from_millis(20));
    assert_eq!(fired.load(Ordering::SeqCst), count);
}

#[test]
fn cancel_before_start_skips_callback() {
    let queue = WorkQueue::new(ExecutionPolicy::Sequenced).expect("create");
    let gate = Arc::new((Mutex::new(false), Condvar::new()));
    let ran = Arc::new(AtomicU32::new(0));

    let blocker_gate = Arc::clone(&gate);
    let _blocker = queue.enqueue(move || {
        let (lock, cv) = &*blocker_gate;
        let mut go = lock.lock().unwrap();
        while !*go {
            go = cv.wait(go).unwrap();
        }
    });

    let r = Arc::clone(&ran);
    let victim = queue.enqueue(move || {
        r.fetch_add(1, Ordering::SeqCst);
    });

    assert!(victim.try_cancel());
    assert_eq!(victim.state(), WorkItemState::Canceled);

    let (lock, cv) = &*gate;
    *lock.lock().unwrap() = true;
    cv.notify_all();

    assert!(queue.wait_for(POLL_BUDGET), "queue did not drain");
    assert_eq!(ran.load(Ordering::SeqCst), 0);
}

#[test]
fn close_drains_in_flight_and_cancels_queued() {
    let queue = WorkQueue::new(ExecutionPolicy::Parallel).expect("create");
    let counter = Arc::new(AtomicU32::new(0));
    for _ in 0..200 {
        let c = Arc::clone(&counter);
        queue.enqueue(move || {
            c.fetch_add(1, Ordering::SeqCst);
        });
    }
    queue.close();
    let after = counter.load(Ordering::SeqCst);
    // No callback may run after close returns.
    std::thread::sleep(Duration::from_millis(20));
    assert_eq!(counter.load(Ordering::SeqCst), after);
    assert!(after <= 200);
}

fn poll_until(mut done: impl FnMut() -> bool) {
    let deadline = std::time::Instant::now() + POLL_BUDGET;
    while std::time::Instant::now() < deadline {
        if done() {
            return;
        }
        std::thread::sleep(Duration::from_millis(2));
    }
    panic!("condition not met within budget");
}
