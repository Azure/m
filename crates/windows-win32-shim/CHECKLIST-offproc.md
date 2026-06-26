# CHECKLIST — off-process interception handling (`windows-win32-shim`)

Staged migration of all interception-response work (journaling today; isolation
decisions later) OFF the calling thread, then OUT of process. Design: `SHIM-D25` in
[DESIGN-NOTES.md](DESIGN-NOTES.md).

The marshaled interaction is a **position-independent JSON** request/reply pair —
the eventual cross-process contract. We proceed piecemeal; interactions grow more
complex over time.

Milestone **OT** (off-thread, synchronous, in-process) is complete — see
[COMPLETED-CHECKLIST.md](COMPLETED-CHECKLIST.md) (Moved 2026-06-09). Each seam now
marshals the raw context, dispatches the journaling worker to a thread-pool work
item, and blocks on a `WaitOnAddress` latch until it finishes.

## Deferred (next stages, not yet planned into milestones)

These seed the next milestones; promote to concrete items when picked up.

- Honor the caller's actual contract (don't always block) — true async for fire-and-forget seams.
- Move the worker OUT of process (a collector); the marshaled JSON becomes the IPC payload. The
  channel then carries raw context → PII tokenization (D-AJ-4 / PII-A) must happen at the worker
  before persisting.
- The reply may carry a *modified* response (redirect / replay / fault), not just an ack.
- Compact the marshaled bodies (base64 or binary framing) instead of JSON byte arrays.
