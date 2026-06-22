// Copyright (c) Microsoft Corporation.

//! [`Executor`]: spawn futures onto the Windows thread pool (EX-D1, EX-D2).

use std::collections::VecDeque;
use std::future::Future;
use std::pin::Pin;
use std::sync::{Arc, Mutex};
use std::task::{Context, Poll};

use async_task::{Runnable, Task};
use windows_threadpool::{ThreadPoolResult, Work};

/// State shared between the executor, its reusable work callback, and every
/// task's schedule closure.
struct Shared {
    /// Runnables that have been woken and are waiting to be polled.
    ready: Mutex<VecDeque<Runnable>>,
}

/// A futures executor backed by the Windows thread pool.
///
/// A spawned future occupies a pool thread only while it is actually being
/// polled: when it returns `Pending` it is parked off-thread until its waker
/// re-schedules it (EX-D1). A single reusable [`Work`] drains the ready queue,
/// so no thread-pool handle is allocated per wake (EX-D2).
pub struct Executor {
    shared: Arc<Shared>,
    work: Arc<Work>,
}

impl Executor {
    /// Create an executor with its reusable thread-pool work item.
    pub fn new() -> ThreadPoolResult<Self> {
        let shared = Arc::new(Shared {
            ready: Mutex::new(VecDeque::new()),
        });

        let drain = Arc::clone(&shared);
        let work = Work::new(move || {
            // Run at most one ready runnable per work callback; each schedule
            // pushes one runnable and submits the work once, so the counts stay
            // balanced (a spurious extra callback simply finds an empty queue).
            let next = drain.ready.lock().unwrap().pop_front();
            if let Some(runnable) = next {
                runnable.run();
            }
        })?;

        Ok(Self {
            shared,
            work: Arc::new(work),
        })
    }

    /// Spawn `future` onto the thread pool, returning a [`JoinHandle`] for its
    /// output.
    ///
    /// Dropping the returned handle without awaiting it cancels the task (the
    /// future is no longer polled), matching `async-task` semantics.
    pub fn spawn<F>(&self, future: F) -> JoinHandle<F::Output>
    where
        F: Future + Send + 'static,
        F::Output: Send + 'static,
    {
        let shared = Arc::clone(&self.shared);
        let work = Arc::clone(&self.work);
        let schedule = move |runnable: Runnable| {
            shared.ready.lock().unwrap().push_back(runnable);
            work.submit();
        };

        let (runnable, task) = async_task::spawn(future, schedule);
        runnable.schedule();
        JoinHandle(task)
    }
}

/// A handle to a spawned task, resolving to the task's output.
///
/// `JoinHandle` is a [`Future`]; it can be `.await`ed on the pool or driven to
/// completion synchronously with [`crate::block_on`] (or [`JoinHandle::join`]).
pub struct JoinHandle<T>(Task<T>);

impl<T> JoinHandle<T> {
    /// Block the calling thread until the task completes and return its output.
    pub fn join(self) -> T {
        crate::block_on(self.0)
    }
}

impl<T> Future for JoinHandle<T> {
    type Output = T;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<T> {
        Pin::new(&mut self.0).poll(cx)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::block_on;

    #[test]
    fn spawn_then_join_returns_output() {
        let executor = Executor::new().expect("create executor");
        let handle = executor.spawn(async { 21 * 2 });
        assert_eq!(handle.join(), 42);
    }

    #[test]
    fn block_on_awaiting_a_spawned_task() {
        let executor = Executor::new().expect("create executor");
        let a = executor.spawn(async { 1 + 2 });
        let b = executor.spawn(async { 3 });
        let total = block_on(async { a.await + b.await });
        assert_eq!(total, 6);
    }

    #[test]
    fn many_concurrent_spawns_all_complete() {
        const N: u32 = 256;
        let executor = Executor::new().expect("create executor");
        let handles: Vec<_> = (0..N).map(|i| executor.spawn(async move { i * 2 })).collect();
        let sum: u32 = handles.into_iter().map(JoinHandle::join).sum();
        assert_eq!(sum, (0..N).map(|i| i * 2).sum());
    }

    #[test]
    fn awaiting_chain_of_spawned_tasks() {
        let executor = Executor::new().expect("create executor");
        let a = executor.spawn(async { 10 });
        let result = block_on(async {
            let first = a.await;
            first + 5
        });
        assert_eq!(result, 15);
    }
}
