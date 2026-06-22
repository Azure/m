# windows-threadpool-executor

A futures executor layered on the Windows thread pool. Idle async tasks tie up
no dedicated threads — the executor's [`async-task`] schedule closure submits a
[`windows-threadpool`] work item, so a task only occupies a pool thread while it
is actually being polled (TP-D3).

- `Executor::spawn(future) -> JoinHandle<T>` — run a `Send + 'static` future on
  the thread pool.
- `block_on(future) -> T` — drive a future to completion on the calling thread
  with a park/unpark waker, independent of the pool.

The crate contains **no** `unsafe` (`#![forbid(unsafe_code)]`); all Win32
`unsafe` lives in the `windows-threadpool` `ffi` module (TP-D4). It is
Windows-only; on other platforms it compiles to nothing.

See `DESIGN-NOTES.md` for design decisions (EX-D-numbers) and the parent
`windows-threadpool` crate for the safe thread-pool primitives this builds on.

[`async-task`]: https://crates.io/crates/async-task
[`windows-threadpool`]: ../windows-threadpool
