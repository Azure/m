# windows-threadpool design notes

Design decisions for the `windows-threadpool` crate. Tier 1: current canonical
decisions. Each decision has a stable TP-D-number.

Keep these notes small and decision-focused.

## Context

A safe Rust wrapper over the Windows thread pool API (`CreateThreadpoolWork` /
`SubmitThreadpoolWork`, `CreateThreadpoolTimer`, `CreateThreadpoolWait`,
`CreateThreadpoolIo`, with cleanup groups / environments). This is the Rust
parity for the C++ `m::threadpool` library (`src/libraries/threadpool/`):
timers, periodic timers, and work queues with a parallel/sequential execution
policy, plus work-item timing.

It is **independent infrastructure**: the `windows-platform-isolation` crate
does **not** depend on it. It is also the foundation brick for a future async
executor crate (see TP-D3).

## Decision index

| ID | Title |
|---|---|
| TP-D1 | Binds to `windows-sys`, owns its own safe wrapper |
| TP-D2 | Submitted work is `FnOnce + Send + 'static`; cleanup groups bound callback lifetime |
| TP-D3 | The async/tokio executor is a separate, later crate |
| TP-D4 | Quarantine `unsafe`: a thin FFI/trampoline module, a safe API above it |

---

## TP-D1 — Binds to `windows-sys`, owns its own safe wrapper

Same rationale as the isolation crate's D1: call the raw Win32 thread pool entry
points via `windows-sys` and own the safe Rust abstraction (RAII handle
wrappers, typed submit APIs, error mapping). We do not use the higher-level
`windows` crate. The behavior is owned by us; `windows-sys` is selected because
it matches that specification.

## TP-D2 — Submitted work is `FnOnce + Send + 'static`; cleanup bounds lifetime

A submitted callback runs on an arbitrary pool thread, so its closure and all
captured state must be `Send + 'static`. The closure is boxed and passed as the
thread pool callback context (`*mut c_void`); an `extern "system"` trampoline
reconstructs and runs it. A cleanup group (or equivalent wait-on-callbacks
teardown) guarantees in-flight callbacks complete before their context is freed,
so there is no use-after-free on shutdown.

Surface shape mirrors the C++ `threadpool_class`: `create_timer`,
`create_periodic_timer`, `create_work_queue` (parallel / sequential policy), and
work items carrying enqueue / start / end timing.

## TP-D3 — The async executor is a separate, later crate

A futures executor layered on the Windows thread pool (so idle async tasks tie
up no threads) is planned as a **separate** crate (working name
`windows-threadpool-executor`), not part of this one. Likely shape: `async-task`
for the executor (its schedule closure submits a thread pool work item) plus
`CreateThreadpoolIo` as the IOCP reactor (no dedicated reactor thread). Reusing
tokio's *ecosystem* unmodified is a harder, deferred question; this crate just
provides the safe thread pool primitives both approaches need.

## TP-D4 — Quarantine `unsafe`: a thin FFI/trampoline module, a safe API above

Same split as the isolation crate's D13, applied here: all `unsafe`, all
`windows-sys` calls, the raw `extern "system"` trampoline (TP-D2), and raw pool
handles live in one small `ffi`/`sys` module that converts them into safe RAII
wrappers and a typed submit API. Everything callers touch — the work-queue /
timer surface, the `FnOnce` submission ergonomics, timing — is in the safe half
(`#![forbid(unsafe_code)]` where possible, `#[allow]` only on the FFI module).
The trampoline is the trickiest unsafe surface, so it is deliberately confined
and small enough to audit by eye.

## Status

No implementation yet (scaffold `lib.rs`). When design transitions to build,
milestones are queued in a CHECKLIST.md at this crate root, cross-referenced to
TP-D1..TP-D3. No work is scheduled by these decisions alone.

**Async driver / sequencing intent.** The real consumer of async is the future
Hostable Web Core (HWC) isolation surface, which has strong async support — not
the registry surface (which stays synchronous, D12). The registry code is not
expected to use async/tokio meaningfully. However, the intent is to get the safe
thread pool primitives here (and enough of the executor story in TP-D3) *working*
during this current cluster of features, so the async foundation is proven before
HWC needs it. Deep HWC async design is deferred to that surface's own session.
