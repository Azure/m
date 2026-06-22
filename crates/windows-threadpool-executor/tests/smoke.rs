// Copyright (c) Microsoft Corporation.

//! Integration smoke tests for the `windows-threadpool-executor` public API:
//! spawn + await chains, `block_on` over spawned tasks, and many concurrent
//! spawns completing, at a larger scale than the in-module unit tests.

#![cfg(windows)]

use std::sync::Arc;
use std::sync::atomic::{AtomicU32, Ordering};

use windows_threadpool_executor::{Executor, block_on};

#[test]
fn many_concurrent_spawns_complete() {
    const N: u32 = 1_000;
    let executor = Executor::new().expect("create executor");
    let counter = Arc::new(AtomicU32::new(0));

    let handles: Vec<_> = (0..N)
        .map(|_| {
            let c = Arc::clone(&counter);
            executor.spawn(async move {
                c.fetch_add(1, Ordering::SeqCst);
            })
        })
        .collect();

    for handle in handles {
        handle.join();
    }
    assert_eq!(counter.load(Ordering::SeqCst), N);
}

#[test]
fn spawned_results_sum_correctly() {
    const N: u32 = 500;
    let executor = Executor::new().expect("create executor");
    let handles: Vec<_> = (0..N).map(|i| executor.spawn(async move { i })).collect();
    let sum: u32 = handles.into_iter().map(|h| h.join()).sum();
    assert_eq!(sum, (0..N).sum());
}

#[test]
fn block_on_awaits_a_chain_of_spawned_tasks() {
    let executor = Executor::new().expect("create executor");
    let result = block_on(async {
        let mut acc = 0u32;
        for i in 0..50u32 {
            acc += executor.spawn(async move { i * 2 }).await;
        }
        acc
    });
    assert_eq!(result, (0..50u32).map(|i| i * 2).sum());
}

#[test]
fn nested_spawn_inside_a_spawned_task() {
    let executor = Executor::new().expect("create executor");
    let inner = executor.spawn(async { 21 });
    let outer = executor.spawn(async move { inner.await * 2 });
    assert_eq!(outer.join(), 42);
}

#[test]
fn block_on_a_join_handle_directly() {
    let executor = Executor::new().expect("create executor");
    let handle = executor.spawn(async { "done" });
    assert_eq!(block_on(handle), "done");
}
