# CHECKLIST — off-process interception handling (`windows-win32-shim`)

Staged migration of all interception-response work (journaling today; isolation
decisions later) OFF the calling thread, then OUT of process. Design: `SHIM-D25` in
[DESIGN-NOTES.md](DESIGN-NOTES.md).

The marshaled interaction is a **position-independent JSON** request/reply pair —
the eventual cross-process contract. We proceed piecemeal; interactions grow more
complex over time.

## Milestone OT — Off-thread, synchronous (in-process)

Move the work to a Windows thread-pool work item and block the calling thread until
it finishes (via a `WaitOnAddress` latch), regardless of the caller's contract. The
persisted journal is unchanged — only *where* the work runs moves.

- [x] **OT-1** Add a `WaitGate` completion latch (`WaitOnAddress` / `WakeByAddressSingle`,
      unsafe quarantined in `ffi`) to `windows-threadpool`; add `windows-threadpool` as a
      `windows-win32-shim` dependency.
      > ➡ **CROSS-COMPONENT:** the `WaitGate` primitive lands in `crates/windows-threadpool`.
- [x] **OT-2** Define the position-independent marshaled interaction format in a new shim
      `marshal` module: a JSON request (the raw intercepted context — seam, method,
      scheme/host/port, path+query, headers, bodies as byte arrays, status) and a JSON reply
      (an outcome/ack), with serde round-trip. (base64 compaction of bodies deferred.)
- [x] **OT-3** A worker function `handle_interaction(request_json, &sink) -> reply_json` that
      performs the journaling (reduce to shapes / safelist headers / optional example, then
      write the record). The reduction moves here from the decorators; the on-disk record is
      byte-identical to today.
- [x] **OT-4** A synchronous off-thread dispatcher: marshal the context → `submit_once` a work
      item that runs `handle_interaction` then signals the `WaitGate` → `wait` → return the reply.
- [ ] **OT-5** Wire `JournalingEgress::send` to marshal + dispatch off-thread instead of the
      inline `sink.record`.
- [ ] **OT-6** Wire `JournalingHandler` (inbound) to marshal + dispatch off-thread.
- [ ] **OT-7** Tests: the off-thread path produces the same journal records as the inline path;
      the `WaitGate` blocks then wakes; the marshaled request/reply round-trips.

## Deferred (next stages, intentionally not now)

- Honor the caller's actual contract (don't always block) — true async for fire-and-forget seams.
- Move the worker OUT of process (a collector); the marshaled JSON becomes the IPC payload. The
  channel then carries raw context → PII tokenization (D-AJ-4 / PII-A) must happen at the worker
  before persisting.
- The reply may carry a *modified* response (redirect / replay / fault), not just an ack.
