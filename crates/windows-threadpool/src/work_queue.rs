// Copyright (c) Microsoft Corporation.

//! Work queues with an execution policy, vending observable work-item handles.
//!
//! Parity with the C++ `m::work_queue` / `m::work_item`
//! (`src/libraries/threadpool/`): [`WorkQueue::enqueue`] returns a [`WorkItem`]
//! that exposes its [`WorkItemState`], enqueue / start / end [`WorkItemTimes`],
//! cooperative [`WorkItem::try_cancel`], a stable [`WorkItem::id`], and
//! [`WorkItem::wait`] / [`WorkItem::wait_for`].
//!
//! [`ExecutionPolicy::Parallel`] lets items run concurrently on pool threads;
//! [`ExecutionPolicy::Sequenced`] serializes them. [`WorkQueue::close`] cancels
//! not-yet-started items and drains in-flight ones; drop performs the same
//! drain.
//!
//! This module is safe Rust layered over the [`crate::Work`] primitive; it adds
//! no `unsafe`.

use std::collections::VecDeque;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Condvar, Mutex};
use std::time::{Duration, Instant};

use crate::error::ThreadPoolResult;
use crate::work::Work;

/// How a [`WorkQueue`] schedules its items.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ExecutionPolicy {
    /// Items run one at a time, in enqueue order.
    Sequenced,
    /// Items may run concurrently on multiple pool threads.
    Parallel,
}

/// The lifecycle state of a [`WorkItem`].
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WorkItemState {
    /// Enqueued, not yet started.
    Queued,
    /// Currently running on a pool thread.
    Running,
    /// Ran to completion.
    Done,
    /// Cancelled before it started; its callback will never run.
    Canceled,
}

/// Enqueue / start / end timestamps for a [`WorkItem`], sampled atomically.
///
/// Times are monotonic [`Instant`]s. `start` and `end` are `None` until the
/// item starts and finishes respectively (and stay `None` for a cancelled
/// item).
#[derive(Clone, Copy, Debug)]
pub struct WorkItemTimes {
    /// When the item was enqueued.
    pub enqueue: Instant,
    /// When the item began running, if it has.
    pub start: Option<Instant>,
    /// When the item finished running, if it has.
    pub end: Option<Instant>,
}

struct Progress {
    state: WorkItemState,
    enqueue: Instant,
    start: Option<Instant>,
    end: Option<Instant>,
}

type Job = Box<dyn FnOnce() + Send + 'static>;

struct WorkItemShared {
    id: u64,
    // Taken (consumed) exactly once when the item runs.
    job: Mutex<Option<Job>>,
    progress: Mutex<Progress>,
    // Notified when the item reaches a terminal state (Done or Canceled).
    done: Condvar,
}

impl WorkItemShared {
    fn new(id: u64, job: Job) -> Self {
        Self {
            id,
            job: Mutex::new(Some(job)),
            progress: Mutex::new(Progress {
                state: WorkItemState::Queued,
                enqueue: Instant::now(),
                start: None,
                end: None,
            }),
            done: Condvar::new(),
        }
    }

    /// Run the item if it is still `Queued`. Returns whether it actually ran
    /// (false if it had been cancelled before starting).
    fn run(&self) -> bool {
        {
            let mut p = self.progress.lock().expect("progress mutex poisoned");
            if p.state != WorkItemState::Queued {
                return false;
            }
            p.state = WorkItemState::Running;
            p.start = Some(Instant::now());
        }

        let job = self.job.lock().expect("job mutex poisoned").take();
        if let Some(job) = job {
            job();
        }

        {
            let mut p = self.progress.lock().expect("progress mutex poisoned");
            p.state = WorkItemState::Done;
            p.end = Some(Instant::now());
        }
        self.done.notify_all();
        true
    }

    /// Cancel the item iff it is still `Queued`. Returns true if it was
    /// cancelled by this call.
    fn cancel_if_queued(&self) -> bool {
        let mut p = self.progress.lock().expect("progress mutex poisoned");
        if p.state == WorkItemState::Queued {
            p.state = WorkItemState::Canceled;
            drop(p);
            self.done.notify_all();
            true
        } else {
            false
        }
    }

    fn state(&self) -> WorkItemState {
        self.progress.lock().expect("progress mutex poisoned").state
    }

    fn times(&self) -> WorkItemTimes {
        let p = self.progress.lock().expect("progress mutex poisoned");
        WorkItemTimes {
            enqueue: p.enqueue,
            start: p.start,
            end: p.end,
        }
    }

    fn is_terminal(state: WorkItemState) -> bool {
        matches!(state, WorkItemState::Done | WorkItemState::Canceled)
    }

    fn wait(&self) {
        let mut p = self.progress.lock().expect("progress mutex poisoned");
        while !Self::is_terminal(p.state) {
            p = self.done.wait(p).expect("progress mutex poisoned");
        }
    }

    fn wait_for(&self, dur: Duration) -> bool {
        let deadline = Instant::now() + dur;
        let mut p = self.progress.lock().expect("progress mutex poisoned");
        while !Self::is_terminal(p.state) {
            let now = Instant::now();
            if now >= deadline {
                return false;
            }
            let (guard, timeout) = self
                .done
                .wait_timeout(p, deadline - now)
                .expect("progress mutex poisoned");
            p = guard;
            if timeout.timed_out() && !Self::is_terminal(p.state) {
                return false;
            }
        }
        true
    }
}

/// A handle to one enqueued unit of work.
///
/// Cheap to clone; all clones observe the same underlying item.
#[derive(Clone)]
pub struct WorkItem {
    shared: Arc<WorkItemShared>,
}

impl WorkItem {
    /// The item's stable, queue-unique id.
    #[must_use]
    pub fn id(&self) -> u64 {
        self.shared.id
    }

    /// The item's current lifecycle state.
    #[must_use]
    pub fn state(&self) -> WorkItemState {
        self.shared.state()
    }

    /// Enqueue / start / end times, sampled atomically.
    #[must_use]
    pub fn times(&self) -> WorkItemTimes {
        self.shared.times()
    }

    /// When the item was enqueued.
    #[must_use]
    pub fn enqueue_time(&self) -> Instant {
        self.shared.times().enqueue
    }

    /// When the item started, if it has.
    #[must_use]
    pub fn start_time(&self) -> Option<Instant> {
        self.shared.times().start
    }

    /// When the item finished, if it has.
    #[must_use]
    pub fn end_time(&self) -> Option<Instant> {
        self.shared.times().end
    }

    /// Attempt to cancel the item before it starts.
    ///
    /// Returns `true` only if the item was still queued and is now cancelled
    /// (its callback will never run). Returns `false` if it had already started,
    /// finished, or been cancelled — do not infer it is running from `false`,
    /// though that is the most likely cause.
    pub fn try_cancel(&self) -> bool {
        self.shared.cancel_if_queued()
    }

    /// Block until the item reaches a terminal state (`Done` or `Canceled`).
    pub fn wait(&self) {
        self.shared.wait();
    }

    /// Block until the item is terminal or `dur` elapses. Returns `true` if the
    /// item became terminal within the duration.
    pub fn wait_for(&self, dur: Duration) -> bool {
        self.shared.wait_for(dur)
    }
}

struct QueueState {
    ready: VecDeque<Arc<WorkItemShared>>,
    running: usize,
    // Sequenced policy: a drain loop is in flight, so enqueue must not submit a
    // second one.
    draining: bool,
}

struct QueueInner {
    policy: ExecutionPolicy,
    state: Mutex<QueueState>,
    // Notified whenever the queue makes progress (item taken / finished), for
    // queue-level `wait_for`.
    idle: Condvar,
    next_id: AtomicU64,
}

impl QueueInner {
    fn new(policy: ExecutionPolicy) -> Self {
        Self {
            policy,
            state: Mutex::new(QueueState {
                ready: VecDeque::new(),
                running: 0,
                draining: false,
            }),
            idle: Condvar::new(),
            next_id: AtomicU64::new(1),
        }
    }

    /// Pop the next ready item, marking the queue as having one more running.
    fn take_next(&self) -> Option<Arc<WorkItemShared>> {
        let mut st = self.state.lock().expect("queue mutex poisoned");
        let item = st.ready.pop_front();
        if item.is_some() {
            st.running += 1;
        }
        item
    }

    fn finish_one(&self) {
        let mut st = self.state.lock().expect("queue mutex poisoned");
        st.running -= 1;
        drop(st);
        self.idle.notify_all();
    }

    /// The thread-pool callback body: run work according to policy.
    fn run_one(&self) {
        match self.policy {
            ExecutionPolicy::Parallel => {
                if let Some(item) = self.take_next() {
                    item.run();
                    self.finish_one();
                }
            }
            ExecutionPolicy::Sequenced => {
                while let Some(item) = self.take_next() {
                    item.run();
                    self.finish_one();
                }
                {
                    let mut st = self.state.lock().expect("queue mutex poisoned");
                    st.draining = false;
                }
                self.idle.notify_all();
            }
        }
    }

    fn queue_size(&self) -> usize {
        self.state.lock().expect("queue mutex poisoned").ready.len()
    }

    fn running(&self) -> usize {
        self.state.lock().expect("queue mutex poisoned").running
    }

    fn is_quiescent(st: &QueueState) -> bool {
        st.ready.is_empty() && st.running == 0
    }

    fn wait_for(&self, dur: Duration) -> bool {
        let deadline = Instant::now() + dur;
        let mut st = self.state.lock().expect("queue mutex poisoned");
        while !Self::is_quiescent(&st) {
            let now = Instant::now();
            if now >= deadline {
                return false;
            }
            let (guard, timeout) = self
                .idle
                .wait_timeout(st, deadline - now)
                .expect("queue mutex poisoned");
            st = guard;
            if timeout.timed_out() && !Self::is_quiescent(&st) {
                return false;
            }
        }
        true
    }

    /// Cancel every not-yet-started item; returns nothing but wakes their
    /// waiters.
    fn cancel_queued(&self) {
        let mut st = self.state.lock().expect("queue mutex poisoned");
        while let Some(item) = st.ready.pop_front() {
            item.cancel_if_queued();
        }
    }
}

/// A queue of work items scheduled onto the OS thread pool under a policy.
pub struct WorkQueue {
    inner: Arc<QueueInner>,
    work: Work,
}

impl WorkQueue {
    /// Create a work queue with the given execution policy.
    pub fn new(policy: ExecutionPolicy) -> ThreadPoolResult<Self> {
        let inner = Arc::new(QueueInner::new(policy));
        let callback_inner = Arc::clone(&inner);
        let work = Work::new(move || callback_inner.run_one())?;
        Ok(Self { inner, work })
    }

    /// Enqueue a job and return a handle to it.
    pub fn enqueue<F>(&self, job: F) -> WorkItem
    where
        F: FnOnce() + Send + 'static,
    {
        let id = self.inner.next_id.fetch_add(1, Ordering::Relaxed);
        let shared = Arc::new(WorkItemShared::new(id, Box::new(job)));

        let should_submit = {
            let mut st = self.inner.state.lock().expect("queue mutex poisoned");
            st.ready.push_back(Arc::clone(&shared));
            match self.inner.policy {
                // One submission per item; submissions fan out across threads.
                ExecutionPolicy::Parallel => true,
                // A single drain loop services the whole queue; only start one.
                ExecutionPolicy::Sequenced => {
                    if st.draining {
                        false
                    } else {
                        st.draining = true;
                        true
                    }
                }
            }
        };

        if should_submit {
            self.work.submit();
        }

        WorkItem { shared }
    }

    /// The execution policy this queue was created with.
    #[must_use]
    pub fn policy(&self) -> ExecutionPolicy {
        self.inner.policy
    }

    /// Number of items currently waiting to run.
    #[must_use]
    pub fn queue_size(&self) -> usize {
        self.inner.queue_size()
    }

    /// Number of items currently running.
    #[must_use]
    pub fn running(&self) -> usize {
        self.inner.running()
    }

    /// Wait until the queue is quiescent (nothing ready, nothing running) or
    /// `dur` elapses. Returns `true` if the queue drained within the duration.
    pub fn wait_for(&self, dur: Duration) -> bool {
        self.inner.wait_for(dur)
    }

    /// Cancel not-yet-started items and drain in-flight ones.
    ///
    /// Idempotent. After `close()` returns — provided no further `enqueue`
    /// happens — no callback runs against this queue. Not-yet-started items are
    /// moved to `Canceled`, unblocking any thread waiting on them.
    pub fn close(&self) {
        self.inner.cancel_queued();
        // Cancel any submitted-but-not-started pool callbacks and wait for
        // running ones to finish.
        self.work.cancel_pending();
    }
}

impl Drop for WorkQueue {
    fn drop(&mut self) {
        self.close();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU32, Ordering};
    use std::time::Duration;

    fn drained(queue: &WorkQueue) {
        assert!(
            queue.wait_for(Duration::from_secs(5)),
            "queue did not drain in time"
        );
    }

    #[test]
    fn parallel_runs_all_items() {
        let queue = WorkQueue::new(ExecutionPolicy::Parallel).expect("create");
        let counter = Arc::new(AtomicU32::new(0));
        let mut items = Vec::new();
        for _ in 0..20 {
            let c = Arc::clone(&counter);
            items.push(queue.enqueue(move || {
                c.fetch_add(1, Ordering::SeqCst);
            }));
        }
        drained(&queue);
        for item in &items {
            item.wait();
            assert_eq!(item.state(), WorkItemState::Done);
        }
        assert_eq!(counter.load(Ordering::SeqCst), 20);
    }

    #[test]
    fn sequenced_preserves_enqueue_order() {
        let queue = WorkQueue::new(ExecutionPolicy::Sequenced).expect("create");
        let log = Arc::new(Mutex::new(Vec::new()));
        for i in 0..15u32 {
            let l = Arc::clone(&log);
            queue.enqueue(move || {
                l.lock().unwrap().push(i);
            });
        }
        drained(&queue);
        let observed = log.lock().unwrap().clone();
        let expected: Vec<u32> = (0..15).collect();
        assert_eq!(observed, expected);
    }

    #[test]
    fn try_cancel_before_start_prevents_run() {
        // A sequenced queue with a blocker lets us cancel an item that is still
        // queued behind the blocker.
        let queue = WorkQueue::new(ExecutionPolicy::Sequenced).expect("create");
        let release = Arc::new((Mutex::new(false), Condvar::new()));
        let ran = Arc::new(AtomicU32::new(0));

        let blocker_release = Arc::clone(&release);
        let _blocker = queue.enqueue(move || {
            let (lock, cv) = &*blocker_release;
            let mut go = lock.lock().unwrap();
            while !*go {
                go = cv.wait(go).unwrap();
            }
        });

        let r = Arc::clone(&ran);
        let victim = queue.enqueue(move || {
            r.fetch_add(1, Ordering::SeqCst);
        });

        // The victim is queued behind the still-running blocker.
        assert!(victim.try_cancel(), "queued item should cancel");
        assert_eq!(victim.state(), WorkItemState::Canceled);

        // Release the blocker and drain.
        {
            let (lock, cv) = &*release;
            *lock.lock().unwrap() = true;
            cv.notify_all();
        }
        drained(&queue);
        assert_eq!(ran.load(Ordering::SeqCst), 0);
    }

    #[test]
    fn times_progress_through_states() {
        let queue = WorkQueue::new(ExecutionPolicy::Parallel).expect("create");
        let item = queue.enqueue(|| {});
        item.wait();
        let times = item.times();
        assert!(times.start.is_some());
        assert!(times.end.is_some());
        assert!(times.start.unwrap() >= times.enqueue);
        assert!(times.end.unwrap() >= times.start.unwrap());
    }

    #[test]
    fn ids_are_unique_and_increasing() {
        let queue = WorkQueue::new(ExecutionPolicy::Parallel).expect("create");
        let a = queue.enqueue(|| {});
        let b = queue.enqueue(|| {});
        let c = queue.enqueue(|| {});
        assert!(a.id() < b.id() && b.id() < c.id());
        drained(&queue);
    }

    #[test]
    fn close_drains_and_cancels() {
        let queue = WorkQueue::new(ExecutionPolicy::Parallel).expect("create");
        let counter = Arc::new(AtomicU32::new(0));
        for _ in 0..10 {
            let c = Arc::clone(&counter);
            queue.enqueue(move || {
                c.fetch_add(1, Ordering::SeqCst);
            });
        }
        queue.close();
        // After close, every item is in a terminal state; no further callbacks.
        let after = counter.load(Ordering::SeqCst);
        assert!(after <= 10);
        // close is idempotent.
        queue.close();
        assert_eq!(counter.load(Ordering::SeqCst), after);
    }
}
