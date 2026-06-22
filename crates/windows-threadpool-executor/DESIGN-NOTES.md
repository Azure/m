# windows-threadpool-executor design notes

Design decisions for the `windows-threadpool-executor` crate. Tier 1: current
canonical decisions. Each decision has a stable EX-D-number.

This crate realizes TP-D3 of the sibling `windows-threadpool` crate: a futures
executor whose schedule step submits a thread-pool work item, so idle async
tasks occupy no dedicated threads.

## Decision index

| ID | Title |
|---|---|
| EX-D1 | `async-task` for task allocation; schedule submits a pooled work item |
| EX-D2 | One reusable `Work` drains a ready queue (no per-wake handle alloc) |
| EX-D3 | `block_on` is a standalone park/unpark driver, independent of the pool |
| EX-D4 | No `unsafe` in this crate; Win32 `unsafe` stays quarantined in `windows-threadpool` |

---

## EX-D1 — `async-task` for task allocation; schedule submits a pooled work item

A spawned future is wrapped by `async_task::spawn`, which yields a `Runnable`
and a `Task<T>`. The schedule closure is invoked each time the task is woken; it
hands the `Runnable` to the thread pool to be polled on a pool thread. We own
the executor behavior (spawn semantics, where polling happens); `async-task` is
selected because its split of allocation (`Task`) from scheduling (the schedule
closure) matches that specification.

## EX-D2 — One reusable `Work` drains a ready queue

Rather than allocate a fresh `CreateThreadpoolWork` handle per wake, the
executor owns a single reusable `windows_threadpool::Work`. The schedule closure
pushes the woken `Runnable` onto a shared ready queue and submits the reusable
work; the work callback pops one `Runnable` and runs it. This avoids a handle
allocation on every poll and avoids the join-on-drop of a one-shot submission
blocking the scheduling thread.

## EX-D3 — `block_on` is a standalone park/unpark driver

`block_on(future)` drives a future to completion on the *calling* thread using a
`std::task::Wake` implementation backed by a `Mutex<bool>` + `Condvar`
(park/unpark). It does not use the thread pool, so it composes with `spawn`:
`block_on` a `JoinHandle` polls the spawned task (which runs on the pool) and
parks until the pool thread completes it and wakes the parker.

## EX-D4 — No `unsafe` in this crate

The executor is `#![forbid(unsafe_code)]`. All raw Win32 calls, trampolines, and
pool-handle lifetime management stay in the `windows-threadpool` `ffi` module
(TP-D4). This crate only composes safe primitives (`Work`, `async-task`, std
waker support).

## Status

Implementation in progress; milestones queued in the sibling
`windows-threadpool/CHECKLIST.md` (M7-2), cross-referenced to EX-D1..EX-D4.
