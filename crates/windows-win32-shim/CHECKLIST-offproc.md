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

## Milestone BC — Bounded, compact marshaled bodies

The worker never inspects past `max_body_bytes`, yet the seam currently marshals the
**entire** raw body and serializes it as a JSON number array (~4× bloat). Bound the
payload at the seam and encode it compactly. Both changes are behavior-preserving for
the on-disk record.

- [x] **BC-1** Cap marshaled bodies at the seam: add `JournalSink::capped_body` (returns the
      leading `min(len, max_body_bytes)` bytes) and apply it to the egress and inbound request /
      response bodies before building the `Interaction`. Behavior-preserving because the worker
      already slices to the same cap. Add an end-to-end test that an over-cap body through the
      off-thread path produces the same record as the inline-equivalent.
- [ ] **BC-2** Encode the marshaled body fields as base64 strings (RFC 4648) instead of JSON
      number arrays: a `base64_bytes` serde `with` module on `Interaction::request_body` /
      `response_body`, keeping `skip_serializing_if`/`default`. Update the round-trip tests and
      assert the JSON carries a base64 string, not a number array.

## Deferred (next stages, not yet planned into milestones)

These seed the next milestones; promote to concrete items when picked up.

- Honor the caller's actual contract (don't always block) — true async for fire-and-forget seams.
- Move the worker OUT of process (a collector); the marshaled JSON becomes the IPC payload. The
  channel then carries raw context → PII tokenization (D-AJ-4 / PII-A) must happen at the worker
  before persisting.
- The reply may carry a *modified* response (redirect / replay / fault), not just an ack.
