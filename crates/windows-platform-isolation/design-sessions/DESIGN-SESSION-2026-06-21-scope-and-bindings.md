# Design session — 2026-06-21 — scope and bindings

Kickoff design session for the `windows-platform-isolation` crate.

**Decisions produced:** D1 (binds to `windows-sys`), D2 (Rust reimplementation
of PIL's surfaces), D3 (registry first), D4 (full decorator stack), D5 (artifact
interop is a hard requirement; shared format jointly replaceable only with
confirmation).

## Starting position

- The crate is a fresh scaffold (`add()` placeholder in `src/lib.rs`).
- It is the Rust-side counterpart to the C++ Platform Isolation Library
  (`src/libraries/pil/`), on the `rusty-pil` branch.
- The `windows-sys`-vs-`windows` binding choice was noted as **not
  functionally design-significant** — a low-stakes, easily reversible
  implementation detail, not a statement about the crate's overall importance.

## Settled this session

### Binding crate: `windows-sys` (→ D1)

We will use `windows-sys` for implementation dependencies rather than the
`windows` crate. Predicted that the higher-level `windows` crate would
otherwise cause repeated friction (ownership/wrapper mismatches, conversion
churn, COM/WinRT machinery we do not need, heavier compile cost) on what is a
thin Win32 data-store seam. Full rationale recorded in DESIGN-NOTES.md D1.

### Relationship to the C++ PIL (→ D2)

Resolved: a **Rust reimplementation** of PIL's surfaces. Not FFI over the C++
PIL, not an independent design — the two are siblings sharing one design, free
to diverge in code. The C++ PIL's DESIGN-NOTES remain the design reference.

### Surfaces (→ D3)

Resolved: **registry first**, mirroring the C++ ordering. Filesystem and others
deferred to later surfaces under the same model.

### Layering (→ D4)

Resolved: adopt the **full** policy-intent decorator stack (pass-through /
buffered / journaling / logging / fault-injecting), same intent as C++ PIL D1.

### Artifact interoperability (→ D5)

Resolved: **full interoperability** with the C++ PIL's artifacts is a **hard
requirement** — both implementations share one persistence format and neither
may diverge to its own. The only flexibility: if maintaining the format becomes
a hardship, both code bases may migrate **together** to a new, superior shared
format (change the C++ code + new Rust code), still fully interoperable. Any
such format change requires explicit confirmation first; likely approved if it
reaches the goal faster, but never done unilaterally.

## String-handling invariants (→ D6–D9)

Directed as key design points that must hold through the implementation:

- **D6** — All case-insensitive comparison uses the Windows ordinal
  case-insensitive comparison (`NORM_IGNORECASE` ordinal; `CompareStringOrdinal`
  with `bIgnoreCase = TRUE`). Never Rust/Unicode case folding.
- **D7** — Internal storage is UTF-16LE for efficiency (the Win32 string APIs
  are UTF-16). Public APIs ingest UTF-8 and return sanitized UTF-8 as usual.
- **D8** — Tabular storage (ordered or unordered) keys on a generated binary
  sort key (`LCMapStringEx` / `LCMAP_SORTKEY` under ordinal `NORM_IGNORECASE`),
  never on case canonicalization.
- **D9** — OS strings may be ill-formed UTF-16. Handling must never panic and
  never lose data: keep raw UTF-16LE internally (lossless), and surface a typed
  error at the UTF-8 boundary (via the standard fallible decode) rather than
  panicking or doing lossy U+FFFD substitution.

All four mirror or extend C++ PIL D12 (ordinal sort keys, never folding stored
case).

## Provider composition (→ D10)

Resolved: typed per-surface facade (`dyn` trait objects) for ergonomics + an
internal reified `Request`/`Surface::invoke` seam where the cross-cutting
decorators (logging, journaling, fault-injection) live, written once and
surface-agnostically. Journaling's verb stream is the `Request` enum; fault
injection matches on `Request`. `Box<dyn>` for runtime composition / open
extension; dispatch cost is irrelevant (syscall-bound). User clarified: we are
borrowing the design **patterns**, not committing to implement specific public
traits.

## Object shapes (→ D11)

Resolved: borrow the *patterns* of `windows-registry` (Key / Value / Type /
OpenOptions / iterators / typed get_/set_) and `std::fs` (File / OpenOptions /
ReadDir / Metadata) — as references, not dependencies, not literal traits.
Justified divergences: roots are **session-vended** (not global `CURRENT_USER` /
`LOCAL_MACHINE` constants) because isolation requires the root to come from the
provider stack; we own our path/string types for D6–D9. (Registry-roots question
resolved: session-vended.)

## Threading (→ D12)

Resolved: the isolation crate is synchronous (Win32 registry/file APIs are sync
and not thread-affine, so async is moot here). Types are `Send` where handles
allow, `Sync` only via explicit `Mutex` (decorator model is `&mut self`). Send
recap: Send = movable across threads; Sync = `&T` shareable across threads.

### Async / thread pool finding

Native asynchrony lives in separate crates, not here. Tokio consolidated its old
separate `tokio-reactor` / `tokio-executor` crates into one `tokio` crate (0.2+);
the reactor is now internal and coupled to the runtime (only `mio` stays
separate). The genuinely modular stack is smol's: `async-task` (executor with a
schedule closure — schedule = submit a thread pool work item), `async-io` /
`polling` (standalone reactor over IOCP). Preferred future shape: an `async-task`
executor whose schedule submits Windows thread pool work + `CreateThreadpoolIo`
as the IOCP reactor (no dedicated reactor thread). Reusing tokio's ecosystem
unmodified is the hard, deferred path.

### `windows-threadpool` crate stood up

Created `crates/windows-threadpool` (Rust parity with C++ `m::threadpool`) with a
minimal DESIGN-NOTES (TP-D1..TP-D3) and a scaffold `lib.rs`, added to the
workspace members. It is independent infrastructure (the isolation crate does
not depend on it) and the foundation brick for the future executor crate.

### Async driver = Hostable Web Core; sequencing intent

User context: the real async consumer is the future **Hostable Web Core (HWC)**
isolation surface, which has good async support. The registry surface stays
synchronous (D12) and is not expected to use async/tokio meaningfully right now.
But there is fairly strong intent to get the thread pool / async foundation
*working* during this current cluster of features (not bolted on later), so it is
proven before HWC needs it. Deep HWC async design is a separate, later
conversation; we want a few basics coded first.

## Error model + first-cut ingress (→ D14, D15)

Resolved: per-surface hand-rolled error types (D14, Rust norm; never shared,
never C++ parity). M1 bounded to the **pure safe core**. Key realization (user:
"clever move"): with no "direct"/live provider in the first cut, the data ingress
is *loading saved state captured by the C++ PIL providers* (read side of D5) \u2014
the Rust side starts as a safe consumer/replayer; capture stays privileged in
C++. That read path is pure safe parsing, so the whole first milestone is FFI-free
and unit-testable (→ D15).

## Next

Ready to draft milestone plans. Create a CHECKLIST.md at the isolation crate root
for **M1 = pure safe core** (overlay/CoW tree, `Request`/`Response` seam,
path/string types per D6\u2013D9, pass-through + buffered decorators, per-surface
errors per D14), ending in an integration test over the tree logic. Open
prerequisite to resolve before queueing the artifact loader: pin down the D5
shared-format spec (read it from the C++ serialization code). Live/direct provider
and write/capture side are later milestones. `windows-sys` enters only when the
FFI leaf lands (D1 / TP-D1).
