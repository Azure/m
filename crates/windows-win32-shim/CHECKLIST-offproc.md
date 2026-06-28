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

Milestone **BC** (bounded, compact marshaled bodies) is complete — see
[COMPLETED-CHECKLIST.md](COMPLETED-CHECKLIST.md) (Moved 2026-06-28). Bodies are capped
at `max_body_bytes` at the seam and base64-encoded in the marshaled JSON.

## Milestone RS — Shore up the in-process cross-thread remoting (do before out-of-process)

Make the off-thread dispatch panic-safe and verified before building the IPC / out-of-process
stage on top of it. A worker panic today is catastrophic: the thread-pool callback trampoline is
`extern "system"` (panic ⇒ process abort), and the dispatcher signals the `WaitGate` as its last
statement (panic before it ⇒ the calling host thread deadlocks on `wait()`). Both violate the
shim's fail-soft contract.

- [x] **RS-1** *(windows-threadpool)* Contain panics at the `extern "system"` callback trampolines
      (`work` / `timer` / `io`) with `catch_unwind` so a panicking callback can never unwind across
      the FFI boundary and abort the process. Test: a `submit_once` callback that panics is
      contained — the pool survives and `Work::wait` returns.
- [x] **RS-2** *(shim)* **PREREQUISITE: RS-1.** Guarantee the completion latch is always signaled:
      signal the `WaitGate` from an RAII guard in the dispatcher's worker body so a worker panic
      wakes the waiter instead of deadlocking it; the waiter then returns a not-journaled `Outcome`.
      Test: a panicking worker wakes the waiter, the host survives, and the result is not-journaled.
- [x] **RS-3** *(shim, optional)* Concurrency stress test: many host threads through
      `dispatch_off_thread` to one sink land every record with no lost wakeups or per-call gate/slot
      cross-talk. (Broad multi-threaded testing was deferred to the OOP stage; this validates the
      in-process machinery specifically — include if the insurance is wanted now.)
- [x] **RS-4** *(decide)* `Outcome.journaled` means *attempted* — `sink.record` swallows write
      errors (fail-soft), so a failed write still reports journaled. **Decided (SHIM-D27):** failure
      policy is mode-dependent — journaling is faithful-or-fatal (capture failure aborts with a clear
      diagnosis; today's swallow is wrong and must become fail-loud); out-of-line primary tasks raise
      a diagnosable fault and the reply carries persisted-vs-dropped; tracing is best-effort, queued
      async, in-process (no OOP round-trip). Implementation tied to the OOP milestones.

Related finding (out of scope here, larger): the shim's exported `extern "system"` functions do not
appear to `catch_unwind`, so a panic on the *calling* thread (marshaling, or the inline
submit-failure fallback) would escape into the host. RS-1 only covers the pool-thread worker;
export-boundary containment is a separate, broader ABI-robustness audit.

## Milestone UT — Untranscoded, encoding-tagged text (spans `api-journal` → shim → `cartographer`)

Stop transcoding captured text in the observed service. The egress seam currently
converts WinHTTP wide strings to UTF-8 eagerly (`to_utf8()`), which burns host CPU,
drops ill-formed UTF-16, and converts header values the worker discards. Carry every
text field as **raw bytes + a 1-byte encoding tag** (`Utf16Le` for WinHTTP/wide,
`Bytes` for HTTP/narrow), persist the tagged bytes, and decode only in `cartographer`
(Option C). Design: `SHIM-D26`. Items are in strict dependency order across components.

**Open design points (work through before implementing):**
- **PII redaction placement.** PII must be redacted *before* the data exits the process, so
  redaction cannot move to the out-of-process collector — it stays in-process (and may *inspect*
  the captured data, which is allowed; only gratuitous re-encoding is not). Where it sits (seam vs.
  in-process pre-send worker) and how it reads the encoding tag is unresolved (D-AJ-4 / PII-A).
- **SOAP support changes the body calculus (reservation — no work scheduled yet).** Extending the
  WireServer dev/test wins to another important Azure agent will require parsing SOAP. Unlike REST,
  a SOAP request's **operation identity** lives in the *body* (first child of `<soap:Body>` and/or
  the `SOAPAction` header), since operations are typically `POST`ed to one endpoint — so the body
  becomes load-bearing for routing/grouping, not just shape/validity, which pulls body inspection
  **in-process** and earlier. Implications to honor when SOAP is built: (a) an XML shape model in
  `cartographer` (today non-JSON bodies reduce to `Opaque`) plus body-based operation identity;
  (b) XML self-describes its encoding in the prolog, which *reinforces* the UT raw-bytes+tag
  decision — never pre-transcode; the XML parser honors the declared encoding; (c) the **BC body
  cap must not truncate before the SOAP operation element** — the `<soap:Body>` child can sit past
  a large envelope/header, so `max_body_bytes` needs a "capture enough to identify the operation"
  rule rather than a blind leading-bytes cut. No UT/BC item changes now; this reserves the design
  space so the future SOAP effort (cartographer XML + shim cap policy) inherits these constraints.
- Other details still under discussion; settle them here before starting UT-A1.

### `api-journal` (schema owner — lands first)

- [ ] **UT-A1** Add a shared `RawStr { enc: TextEnc, bytes: Vec<u8> }` (`TextEnc ∈ { Utf16Le, Bytes }`)
      with constructors `from_utf16_units(&[u16])` / `from_bytes(&[u8])` / `from_utf8(&str)` and a
      lossy `to_string_lossy() -> String` decoder. serde emits a **uniform tagged object**
      `{ "enc": "u16"|"raw", "b64": "…" }` — never sniff the bytes for UTF-8 validity (that buys only
      journal readability, which has no value to the producer; SHIM-D26). Unit tests: round-trip
      both encodings, ill-formed UTF-16 preserved verbatim, lossy decode.
- [ ] **UT-A2** Switch the `JournalRecord` text fields (`method`, `scheme`, `host`, `path`),
      `HeaderField { name, value }`, and `QueryParam.name` from `String` / `Option<String>` to
      `RawStr` / `Option<RawStr>`. Update `infer_scalar` / `derive_example` call sites and the
      schema doc; record the breaking on-disk format change in `api-journal` DESIGN-NOTES. Update
      api-journal tests.
      > ➡ **CROSS-COMPONENT HANDOFF:** next work is in `crates/windows-win32-shim` → UT-B1.

### `windows-win32-shim` (producer)

- [ ] **UT-B0** Stop serializing captured data on the calling thread: `dispatch_off_thread` hands
      the worker the raw in-memory `Interaction` (a move — zero encoding) instead of calling
      `interaction.to_json()` on the host; the worker serializes/`base64`s off-thread (and only at
      the real IPC boundary once out of process). The in-process *reply* serialization is unchanged
      (control data, not captured platform data). This item is shim-local and may land before UT-B1.
- [ ] **UT-B1** **CROSS-COMPONENT PREREQUISITE:** `api-journal` UT-A2 must land first. Change the
      `marshal::Interaction` text fields to `RawStr`. Capture without transcoding: egress wraps
      `Utf16::as_units()` as `Utf16Le` (delete the `to_utf8()` calls in `egress_interaction` /
      `raw_egress_headers`); inbound wraps its narrow bytes as `Bytes`. Update the `marshal`
      round-trip tests.
- [ ] **UT-B2** Build the tagged `JournalRecord` in the worker without transcoding the stored
      fields; reductions that need UTF-8 string ops (`split_path_query`, header safelist,
      content-type match, `infer_scalar`) operate on a transient decoded view only. Update the
      worker + decorator parity tests for the tagged record.
      > ➡ **CROSS-COMPONENT HANDOFF:** next work is in `crates/cartographer` → UT-C1.

### `cartographer` (consumer)

- [ ] **UT-C1** **CROSS-COMPONENT PREREQUISITE:** shim UT-B2 must land first. Decode `RawStr` per
      tag (`to_string_lossy`) wherever cartographer consumes record text (path templating / OpenAPI
      synthesis / grouping). Update fixtures and tests for the tagged journal format.

## Deferred (next stages, not yet planned into milestones)

These seed the next milestones; promote to concrete items when picked up.

- Honor the caller's actual contract (don't always block) — true async for fire-and-forget seams.
- Move the worker OUT of process (a collector); the marshaled JSON becomes the IPC payload. The
  channel then carries raw context → PII tokenization (D-AJ-4 / PII-A) must happen at the worker
  before persisting.
- The reply may carry a *modified* response (redirect / replay / fault), not just an ack.
