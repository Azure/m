# windows-platform-isolation design notes

Design decisions for the `windows-platform-isolation` crate. Tier 1: current
canonical decisions. Each decision has a stable D-number.

Keep these notes small and decision-focused; do not let them grow into a
tutorial.

## Context

This is the Rust-side counterpart to the C++ Platform Isolation Library
(`src/libraries/pil/`). PIL's purpose is to disconnect product code from the
real Windows platform so scenarios can be recorded, replayed from a known
starting point without touching persistent platform state, and fault-injected.
This crate is a Rust **reimplementation** of PIL's surfaces (D2): it ports the
design rather than binding to the C++ implementation.

## Decision index

| ID | Title |
|---|---|
| D1 | Implementation binds to `windows-sys`, not `windows` |
| D2 | This crate is a Rust reimplementation of PIL's surfaces (sibling, not FFI) |
| D3 | Registry is the first surface (mirrors the C++ ordering) |
| D4 | Adopt the full policy-intent decorator stack |
| D5 | Artifact interoperability with the C++ PIL is a hard requirement; the shared format may be jointly replaced only with confirmation |
| D6 | Case-insensitive comparison is Windows ordinal (`NORM_IGNORECASE`), never Unicode folding |
| D7 | Internal storage is UTF-16LE; public APIs are UTF-8 |
| D8 | Tabular storage keys are binary sort keys, never case canonicalization |
| D9 | OS strings may be ill-formed: never panic, never lose data |
| D10 | Provider composition: typed `dyn` facade + internal reified `Request`/`Surface` seam |
| D11 | Object shapes borrow patterns from `windows-registry` / `std::fs` (references, not deps) |
| D12 | Isolation crate is synchronous; `Send` yes, `Sync` only via `Mutex`; async lives in sibling crates |
| D13 | Quarantine `unsafe`: a thin unsafe FFI half, a large safe half holding all logic |
| D14 | One hand-rolled error type per surface trait; never shared, never C++ parity |
| D15 | First cut has no live/"direct" provider; ingress = loading saved C++ provider state |

---

## D1 — Implementation binds to `windows-sys`, not `windows`

**Decision.** This crate's implementation calls the raw Win32 platform APIs
and owns its own Rust abstractions over them. The chosen binding crate is
[`windows-sys`](https://crates.io/crates/windows-sys) — raw `extern "system"`
declarations, constants, and handle type aliases — in preference to the
higher-level [`windows`](https://crates.io/crates/windows) crate.

**Rationale.**

- The job here is a thin isolation seam over a handful of Win32 data-store
  APIs. We supply our own wrapper types, error mapping, and lifetimes; the
  `windows` crate's RAII wrappers, `Result`, and `Param` conversions would
  duplicate or fight that layer rather than help it.
- `windows-sys` is `#![no_std]`-friendly, has a minimal dependency footprint,
  and compiles dramatically faster than `windows`, keeping build cost low.
- We do not need COM or WinRT here (the differentiating capability of the
  `windows` crate). The data-store surfaces PIL isolates are plain Win32.
- Empirically, reaching for the higher-level crate on a low-level seam tends
  to produce repeated friction (wrapper/ownership mismatches, conversion
  churn). Pinning to `windows-sys` up front avoids that.

**This behavior is owned by us, not inherited from the dependency.** Our
specification is "call the raw Win32 entry points and wrap them ourselves";
`windows-sys` was selected because its behavior matches that specification. If
a future need genuinely requires COM/WinRT, that is a new decision to revisit
here — not a silent switch to `windows`.

**This is a low-stakes, easily reversible choice.** The `windows-sys`-vs-
`windows` decision is not functionally design-significant: both are generated
from the same Windows metadata and the entry-point names line up closely, so
switching later is mechanical. We pin `windows-sys` to avoid predictable
friction, not because anything in the design hinges on it.

**No work scheduled by this decision yet.** D1 is forward guidance for the
implementation that does not exist yet; it is a reservation, not a queued task.
The `windows-sys` dependency will be added to `Cargo.toml` by the first
checklist item that needs it, at which point a CHECKLIST.md will reference D1.

## D2 — This crate is a Rust reimplementation of PIL's surfaces

This crate **reimplements** PIL's isolation surfaces in idiomatic Rust. It is
not FFI over the C++ PIL, and not an independent design: it ports the C++ PIL's
conceptual model (a policy-intent decorator stack over a real platform) into
Rust code. The two implementations are **siblings that share a design** and are
free to diverge in code, language idiom, and internal structure.

Consequence: the C++ PIL's DESIGN-NOTES (`src/libraries/pil/DESIGN-NOTES.md`)
are the design reference for what each surface and layer *means*; this crate
restates only the decisions where the Rust side diverges or needs its own
canonical statement.

## D3 — Registry is the first surface

The registry surface is implemented first, mirroring the C++ ordering (C++ PIL
D1/D9: registry first, filesystem second, others later). Other surfaces
(filesystem, …) are deferred and will be added as separate surfaces under the
same decorator model when scheduled.

## D4 — Adopt the full policy-intent decorator stack

This crate adopts the **full** decorator stack from C++ PIL D1, named by policy
intent and layered over the real platform:

| Layer | Intent |
|---|---|
| pass-through | forward reads and writes; persists nothing |
| buffered | land writes in an in-memory overlay, mirror touched keys whole; persists a sealed snapshot |
| journaling | forward + append an ordered, replayable verb stream |
| logging | forward + append a requested-vs-done trace; a side diagnostic, **never** part of a persisted artifact |
| fault-injecting | consult a counted-rule fault script |

The semantics of each layer follow the C++ PIL decisions (notably: buffered =
sealed whole-key snapshot with no negative space; logging is a tap injectable
at any depth and never persisted; fault-injecting consumes a counted-rule
script). The Rust side will restate any layer semantics only where it diverges.

## D5 — Artifact interoperability with the C++ PIL is a hard requirement

Persisted/captured artifacts (buffered-state snapshots, journals) **must
interoperate** with the C++ PIL: an artifact written by either implementation
must be readable by the other. The two implementations share **one** persistence
format. This is a **hard requirement**, not a goal — neither side may
unilaterally diverge to its own format.

The single escape hatch: if, during implementation, maintaining the current
shared format proves a genuine hardship, we may migrate **both** code bases
together to a **new, superior shared format** — change the C++ code and write the
Rust code against the new format — so both implementations still interoperate
(and the user is better off). This is a **joint replacement of the shared
format**, never a per-side divergence; full interoperability still holds
afterward, just on the new format.

**Such a format change requires explicit confirmation before proceeding.** If a
new format reaches the goal faster it will likely be approved, but it must be
asked for and granted first — never change the persistence format unilaterally.

## D6 — Case-insensitive comparison is Windows ordinal, never Unicode folding

All case-insensitive string comparison in this crate **must** use the Windows
ordinal case-insensitive comparison — the same semantics the OS itself uses for
registry keys, value names, and case-insensitive filesystem names. Concretely
this is the ordinal comparator under `NORM_IGNORECASE`; in the Win32 surface
that is `CompareStringOrdinal` with `bIgnoreCase = TRUE`: a fixed,
locale-independent uppercase mapping over UTF-16 code units — **not** linguistic
or locale-sensitive collation.

It is **never** acceptable to substitute a Rust-native or Unicode-native
mechanism — `str::eq_ignore_ascii_case`, `to_lowercase` / `to_uppercase`,
Unicode case folding, ICU, etc. Those disagree with the OS at exactly the
characters that determine correctness, and the purpose of an isolation layer is
to agree with the platform it shadows. (Mirrors C++ PIL D12: case-insensitivity
via ordinal comparison, never by folding stored case.)

**FFI-free seam (D13).** `CompareStringOrdinal` is an FFI call, yet the safe
core must build and test without FFI. The ordinal comparison is therefore
abstracted behind a trait (the "ordinal casing" seam); the **only production
implementation is the mandated Win32 one**. A pure-Rust comparator may exist for
unit tests, but it is **test-only and must never be shipped as the production
comparator** — doing so would violate this decision. The production Win32-backed
implementation lands with the FFI leaf milestone, not in the safe-core milestone.

## D7 — Internal storage is UTF-16LE; public APIs are UTF-8

The Win32 string entry points (comparison, sort-key generation, registry,
filesystem) operate on UTF-16 (`WCHAR`). To make D6 and D8 efficient — no
re-transcoding on every compare or key generation — strings are **stored
internally as UTF-16LE** (`Vec<u16>` / `[u16]`).

Public API boundaries remain idiomatic Rust:

- **Ingress:** public APIs accept UTF-8 (`&str` / `String`), transcoded to
  UTF-16LE once on the way in.
- **Egress:** public APIs return sanitized UTF-8 (`String`), transcoded from
  the UTF-16LE store on the way out (subject to D9).

UTF-16LE is an internal representation detail and must not leak into the public
surface.

## D8 — Tabular storage keys are binary sort keys, never case canonicalization

Any tabular storage — ordered (sorted maps, indexes) or unordered (hash maps,
sets) — that needs a case-insensitive key **must** key on a **binary sort key**
generated by the platform, not on a case-canonicalized copy of the string.

That is: generate the OS sort key (`LCMapStringEx` with `LCMAP_SORTKEY` under
ordinal `NORM_IGNORECASE`) once and use those opaque bytes as the map / index
key. Do **not** uppercase/lowercase the string and store or compare the folded
form ("case canonicalization"), and do not call the pairwise comparator inside a
container's hot path.

Rationale: the binary sort key yields correct ordinal case-insensitive equality
*and* ordering with a plain byte comparison (`memcmp`), is stable, and matches
the OS; folding the stored case loses information and can disagree with the
comparator. (Mirrors C++ PIL D12: ordinal sort keys, never by folding stored
case.)

Sort-key generation (`LCMapStringEx`) is FFI and shares D6's seam: it is
abstracted behind the same "ordinal casing" trait, production is the Win32 impl,
any pure-Rust sort key is test-only and lands behind the FFI leaf milestone.

## D9 — OS strings may be ill-formed: never panic, never lose data

Strings obtained from the operating system are sequences of `u16` and are **not
guaranteed to be well-formed UTF-16** (unpaired surrogates do occur in real
registry and filesystem names). Two outcomes are **forbidden** when handling
them:

- **(a) Panicking** — no `unwrap` / `expect` / slicing that aborts on
  ill-formed input.
- **(b) Losing data** — no lossy replacement (`from_utf16_lossy`, U+FFFD
  substitution) that silently discards or mangles the original code units.

Therefore:

- **The internal store keeps the raw UTF-16LE losslessly.** A `Vec<u16>` holds
  an ill-formed sequence verbatim, so ingest from the OS never fails and never
  drops data. Comparison (D6) and sort-key generation (D8) operate on these raw
  code units — exactly what the OS does.
- **The UTF-8 boundary is where well-formedness is enforced.** When a value
  must be handed out as public UTF-8 and the stored UTF-16 is ill-formed,
  return a **typed error** (an `Err` in the function's `Result`) rather than
  panicking or substituting. Use the standard fallible decode
  (`char::decode_utf16`, which yields `Result<char, DecodeUtf16Error>`) and map
  failures to the crate's error type.

This preserves both round-trip fidelity for platform operations and Rust's
`str` well-formedness guarantee at the public surface, without either forbidden
failure mode.

## D10 — Provider composition: typed `dyn` facade + reified `Request` seam

Providers compose as a stack, mirroring the C++ v-table decorators, but split
across two levels:

- **Public surface = a typed, object-safe trait per surface** (`Registry`, later
  `Filesystem`), composed as `Box<dyn _>` so stacks are built at runtime and
  anyone can add a provider by implementing the trait (open extension). Dispatch
  is one vtable indirection — irrelevant, every call is syscall-bound.
- **Internal decorator seam = a reified operation model**: an operation is a
  value (`Request` / `Response`) passed through `trait Surface { fn invoke(&mut
  self, req: &Request) -> Result<Response, Error>; }`. The cross-cutting layers
  (logging, journaling, fault-injection) are written **once**, surface-agnostic.
  Journaling's verb stream *is* the `Request` enum (D4); fault injection is a
  `match` on `Request` against the counted-rule script. Pass-through and
  buffered carry surface-specific semantics.

The typed facade methods **lower** into `Request`s at the seam, so callers get
ergonomics while the providers stay uniform. Static generics are used only where
a fixed stack is genuinely wanted.

## D11 — Object shapes borrow patterns from `windows-registry` / `std::fs`

The public object vocabulary borrows the **design patterns** of the
[`windows-registry`](https://crates.io/crates/windows-registry) crate (`Key`,
`Value`, `Type`, `OpenOptions`, typed `get_*` / `set_*`, key/value iterators)
and `std::fs` / `std::path` (`File`, `OpenOptions`, `ReadDir`, `Metadata`). Both
are **shape references, not dependencies, and not literal traits to implement**
— they cannot route through our provider stack and may not meet D9, which is the
good cause to reimplement their shape rather than depend on them.

Justified divergences from those references:

1. **Roots are session-vended, not global constants.** `windows-registry`
   exposes `CURRENT_USER` / `LOCAL_MACHINE` as globals bound to the real
   machine; isolation requires the root to come from the provider stack
   (`session.local_machine().open(path)`), which may be a buffered or redirected
   world. This is the crux of isolation and the single biggest divergence.
2. **We own our path/string types** to honor D6–D9 (ordinal case-insensitivity,
   UTF-16LE storage, fallible UTF-8 egress). `std::path::Path` is `OsStr`-based
   with non-ordinal casing and cannot express D6/D8.
3. **Typed accessors keep their shape** (`get_string -> Result<String>`) but the
   error variant covers ill-formed UTF-16 (D9), and the facade lowers into the
   D10 `Request` seam.

## D12 — Isolation crate is synchronous; `Send` yes, `Sync` via `Mutex`

The Win32 registry/file APIs this crate wraps are **synchronous and not
thread-affine** (an `HKEY`/`HANDLE` is usable from any thread), so async is moot
*here*. The threading stance:

- Provider/session types are **`Send`** where the underlying handles allow
  (registry handles qualify) — a session can be moved to another thread.
- They are **not `Sync`** by default: the decorator model is `&mut self`
  (exclusive access), so genuinely shared use goes through an explicit `Mutex`.
- Native asynchrony (Windows thread pool, IOCP, a futures executor) lives in
  **separate crates** (`windows-threadpool`, and a future
  `windows-threadpool-executor`) — never in the isolation surface.

`Send`/`Sync` recap: **`Send`** = a value can be *moved* to another thread;
**`Sync`** = a `&T` can be *shared* across threads (`T: Sync` ⇔ `&T: Send`).
Both are auto-derived; we only intervene when wrapping a raw handle.

## D13 — Quarantine `unsafe`: a thin unsafe FFI half, a large safe half

The whole effort is structured as **two halves**:

- a **thin unsafe half** — as few modules as possible (ideally one `ffi`/`sys`
  module per crate) that is the *only* place `unsafe`, `windows-sys` calls, and
  raw handles/pointers appear. Its sole job is to convert raw OS primitives into
  safe RAII wrapper types and to map error codes; it contains **no stateful
  logic**.
- a **large safe half** — everything else: the decorator stack (D4), the
  `Request`/`Response` seam (D10), path/string types and their invariants
  (D6–D9), journaling, fault injection, the typed facades (D11). This half is
  `#![forbid(unsafe_code)]` (or `deny`) wherever the toolchain allows it, with
  `#[allow(unsafe_code)]` granted only to the designated FFI module.

Rationale (the reason the port exists): we are not rewriting C++ in Rust for its
own sake. The HWC engine in particular has many stateful moving parts, and the
point of choosing Rust (over the obvious alternative, C#) is to hold that state
in **memory-safe** code. If the stateful logic lived in `unsafe`, that rationale
collapses. So the safe half must own all the state and logic, and the unsafe
half must stay small enough to audit by eye. The safe RAII handle types are the
boundary: unsafe code constructs them, safe code only consumes them.

Crucially, the **substance** of isolation is safe-half work, not just glue. The
registry isolation layers \u2014 the buffering/redirection logic and the in-memory
**trees** that represent overlaid, copy-on-write, or merged state \u2014 are
expressible as pure safe algorithms over safe data structures. The same holds
for the filesystem surface. `unsafe` is needed only at the leaves where the stack
actually reads or writes the real OS registry / filesystem; the overlay trees,
merge/diff logic, journaling, and decorator composition above those leaves are
entirely safe. This is why the split is realistic and not merely aspirational:
the stateful machinery we are porting for memory safety genuinely lives in the
safe half.

This is an architectural invariant for **every** crate in this effort, not just
this one (see TP-D4 for the thread pool crate).

## D14 — One hand-rolled error type per surface trait

Following the Rust norm, **each surface trait defines its own error type** — a
hand-rolled `enum` per trait/module (e.g. `RegistryError`, `FilesystemError`),
exposed as the trait's associated `Error` (or the module's `Result<T>` alias).
Each enum covers exactly its surface's failure modes, including the
ill-formed-UTF-16 egress error required by D9. Cross-layer conversions are
explicit (`From`).

We deliberately do **not**: share one crate-wide god-error, depend on a shared
error crate, or mirror the C++ `error_handling` types. Each surface's contract
stays self-describing. (`thiserror` may be used as a derive convenience without
violating "hand-rolled" — the variants are still authored by us; this is an
implementation detail, not a shared-type dependency.)

## D15 — First cut has no live provider; ingress is saved C++ provider state

The first implementation has **no "direct" (live OS) provider**. Its data ingress
is **loading saved state produced by the C++ PIL providers** — the read side of
the shared artifact format (D5). The C++ side performs the privileged capture of
real registry / filesystem state; the Rust side is initially a safe
**consumer/replayer** of that captured state.

This is deliberate and convenient: deserializing the artifact is pure safe
parsing (bytes → overlay tree), so it sits entirely in the safe half (D13) and
needs no FFI, letting the whole first milestone be built and unit-tested with no
live registry and no `unsafe`. Consequences: the live/direct provider and the
write/capture side are later milestones; and the read path makes the D5
shared-format spec a prerequisite for the milestone that adds the loader.

## Status

The kickoff questions (relationship, surfaces, layering, interop) are resolved
as D2–D5. D6–D9 are **binding string-handling invariants**. D10–D12 settle
provider composition, object shapes, and the threading stance (registry roots
are session-vended). Implementation does not exist yet; when design transitions
to build, milestones are queued in a CHECKLIST.md at this crate root and
cross-referenced to D1–D12. The Windows thread pool / async work lives in the
sibling `windows-threadpool` crate (and a future executor crate), not here. No
work is scheduled by these decisions alone.
