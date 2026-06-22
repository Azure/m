# windows-threadpool — CHECKLIST

Action-only checklist. Completed groups move to `COMPLETED-CHECKLIST.md`.
Decision references point at `DESIGN-NOTES.md` (TP-D-numbers).

This is the detailed execution plan for milestone **M7** of the broader
`windows-platform-isolation` roadmap (the M7 block there is the placeholder /
outline; this file is authoritative). The crate is Windows-only infrastructure
and is **not** depended on by `windows-platform-isolation` (TP-D3 context).

Surface parity target: the C++ `m::threadpool` library
(`src/libraries/threadpool/`) — `create_timer`, `create_periodic_timer`,
`create_work_queue` (sequenced / parallel), and work items carrying
enqueue / start / end timing, state, cancel, and wait.

---

## M7-1 — `windows-threadpool` safe core (CreateThreadpoolWork / timers, TP-D2/TP-D4)

- [x] **M7-1.1** Wire `Cargo.toml`: depend on `windows-sys` (TP-D1) with the
      thread-pool / foundation features; add the crate-level lint posture
      (`#![deny(unsafe_code)]` at the root, `#[allow(unsafe_code)]` only on the
      `ffi` module — TP-D4). Add a `ThreadPoolError` mapping Win32 `GetLastError`
      into a safe error type, and an `ffi` module skeleton that owns the only
      `unsafe` in the crate.
- [x] **M7-1.2** RAII `Work` wrapper over `CreateThreadpoolWork` /
      `SubmitThreadpoolWork` / `WaitForThreadpoolWorkCallbacks` /
      `CloseThreadpoolWork`, plus the `extern "system"` trampoline that
      reconstructs and runs a boxed `FnOnce + Send + 'static` (TP-D2). Expose a
      safe `submit(FnOnce + Send + 'static)` entry that submits one-shot work and
      guarantees no use-after-free on teardown (wait-for-callbacks before the
      boxed context is freed).
- [x] **M7-1.3** One-shot `Timer` and `PeriodicTimer` over
      `CreateThreadpoolTimer` / `SetThreadpoolTimer` / `IsThreadpoolTimerSet` /
      `WaitForThreadpoolTimerCallbacks` / `CloseThreadpoolTimer`, mirroring the
      C++ relative-FILETIME due-time computation and the periodic period; `set`,
      `is_set`, `cancel`, `wait`.
- [x] **M7-1.4** `WorkQueue` with `ExecutionPolicy { Sequenced, Parallel }`
      vending `WorkItem` handles that expose `state()`
      (`Queued/Running/Done/Canceled`), enqueue / start / end `times()`,
      `try_cancel()`, `id()`, and `wait()` / `wait_for()` — parity with the C++
      `work_queue` / `work_item`. Sequenced policy serializes; parallel policy
      runs concurrently. `close()` drains in-flight work and cancels
      not-yet-started items.
- [x] **M7-1.5** *(integration)* Threadpool smoke tests: submit-and-join,
      one-shot timer fire, periodic timer N-fires-then-cancel, sequenced vs
      parallel ordering, work-item cancel-before-start, and `close()` drain.

## M7-2 — `windows-threadpool-executor` (futures executor, TP-D3) ⟶ new crate

- [x] **M7-2.1** Scaffold `crates/windows-threadpool-executor`: `Cargo.toml`
      (workspace member, depends on `windows-threadpool` + `async-task`),
      `#![forbid(unsafe_code)]`, README + `DESIGN-NOTES.md`. Add to workspace
      `members`.
- [x] **M7-2.2** `block_on(future) -> T` driving a future to completion on the
      calling thread (park/unpark waker), independent of the pool. Ordered
      before the executor so the executor's tests can `block_on` a `JoinHandle`.
- [x] **M7-2.3** `Executor` whose `async-task` schedule closure submits a
      thread-pool work item (TP-D3): `spawn(future) -> JoinHandle<T>` running
      idle tasks on no dedicated thread.
- [ ] **M7-2.4** *(integration)* Executor smoke tests: spawn + await a chain of
      tasks, `block_on` a future that awaits a spawned task, many concurrent
      spawns complete.

## M7-3 — IOCP reactor (CreateThreadpoolIo, TP-D3)

- [ ] **M7-3.1** RAII `Io` wrapper over `CreateThreadpoolIo` /
      `StartThreadpoolIo` / `CancelThreadpoolIo` / `CloseThreadpoolIo` with the
      `extern "system"` IO-completion trampoline, in the `ffi` quarantine.
- [ ] **M7-3.2** Reactor binding a `HANDLE` to the pool and completing an
      overlapped operation by waking the awaiting task (no dedicated reactor
      thread).
- [ ] **M7-3.3** *(integration)* IOCP completion smoke test over a real
      overlapped handle (e.g. a named pipe or file), asserting the awaited
      future resolves on completion.

## M7-4 — Cross-crate integration

- [ ] **M7-4** *(integration)* End-to-end async smoke: executor + IOCP reactor
      drive an overlapped I/O to completion via `block_on`, plus timer-fire and
      spawn+await in one scenario.
