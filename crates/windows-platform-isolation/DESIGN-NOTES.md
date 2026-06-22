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
| D1 | FFI bindings are confined to leaf `-sys` crates; the effort binds to `windows` |
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
| D13 | Quarantine `unsafe` in leaf `-sys` crates; every other crate forbids `unsafe` |
| D14 | One hand-rolled error type per surface trait; never shared, never C++ parity |
| D15 | First cut has no live/"direct" provider; ingress = loading saved C++ provider state |
| D16 | `windows-text`: standalone reusable Windows string crate (ordinal casing, code pages, transcoding) |
| D17 | HWC redirection runs out of process: named-pipe service + async (MPMC→threadpool→IOCP) capture/validation/injection pipeline |
| D18 | The shared C++ PIL registry artifact is pugixml `<Platform><Registry>` XML (documented schema; D5 read side) |
| D19 | Rust loader mapping: normalize hives to canonical names, fold a sealed snapshot into a base `Hive`, decode `type`/`data` per `reg_value_type` |

---

## D1 — FFI bindings are confined to leaf `-sys` crates; the effort binds to `windows`

**Decision.** All `unsafe` FFI lives in dedicated **leaf `-sys` crates** (D13,
Option B); every other crate is unconditionally `#![forbid(unsafe_code)]`.
Because the binding can no longer leak past a single-purpose leaf, the choice of
binding crate is a **local, low-stakes** decision. The effort **standardizes on
[`windows`](https://crates.io/crates/windows)** as that binding, with
[`windows-sys`](https://crates.io/crates/windows-sys) permitted for a leaf that
genuinely needs nothing `windows` offers and wants minimal weight.

**Why this reverses the earlier lean toward `windows-sys`.** The original
argument for `windows-sys` was that the `windows` crate's RAII/`Result`/`Param`
conveniences would "fight" our wrapper layer and leak ergonomic friction across
the codebase. Option B removes that concern at the root: the binding is visible
only inside a tiny leaf crate, so it cannot fight or leak anywhere else. With
friction off the table, the remaining factors favor `windows`:

- **It shrinks the hand-written `unsafe`.** For handle-owning leaves (registry
  `HKEY`, file `HANDLE`, thread pool), `windows`'s owned handle types and
  `Result`-returning entry points replace hand-rolled `Drop` impls and
  `GetLastError` mapping — the most error-prone glue, in the one place `unsafe`
  is allowed.
- **It future-proofs COM/WinRT.** The HWC surface is likely to need COM;
  `windows` covers that, `windows-sys` does not.
- **Consistency.** One binding across all leaves is simpler than a per-leaf mix.

**Honest caveat.** The `windows-text-sys` leaf is **handle-free** — its calls
(`CompareStringOrdinal`, `LCMapStringEx`, `MultiByteToWideChar` /
`WideCharToMultiByte`) operate on caller-provided buffers and return values or
owned bytes, with no OS handles to own — so it gains little from `windows`'s
RAII handle types beyond uniformity, and pays a little compile weight. That cost
is localized to the leaf and judged worth the consistency; a maintainer may pin
that single leaf to `windows-sys` if the weight ever matters.

**Behavior is owned by us (Design-Autonomy).** Our specification is "call the
raw Win32 entry points and wrap them ourselves in a leaf crate"; `windows` is
selected because it satisfies that specification with the least hand-written
`unsafe`. Both bindings are generated from the same Windows metadata, so the
choice stays low-stakes and mechanically reversible.

**Work scheduled.** The first leaf crate (`windows-text-sys`,
CHECKLIST M2) adds the `windows` dependency; later leaves (registry M5, thread
pool, HWC) follow the same rule.

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
comparator** — doing so would violate this decision. The trait and its Win32
production implementation live in the standalone `windows-text` crate
(D16), not in this crate's safe-core milestone.

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
any pure-Rust sort key is test-only. Both live in the `windows-text`
crate (D16).

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

## D13 — Quarantine `unsafe` in leaf `-sys` crates; every other crate forbids `unsafe`

The whole effort is structured as **two halves**:

- a **thin unsafe half** — a dedicated **leaf `-sys` crate** (Option B; e.g.
  `windows-text-sys`) that is the *only* place `unsafe`, the `windows`
  binding calls (D1), and raw handles/pointers appear. Its sole job is to convert
  raw OS primitives into safe wrapper types / functions and to map error codes;
  it contains **no stateful logic** (the buffer-sizing intrinsic to a two-call
  FFI like `LCMapStringEx` does not count — it is inseparable from the call).
- a **large safe half** — everything else: the decorator stack (D4), the
  `Request`/`Response` seam (D10), path/string types and their invariants
  (D6–D9), journaling, fault injection, the typed facades (D11). Every crate
  outside the leaf `-sys` crates is **unconditionally `#![forbid(unsafe_code)]`**
  — no per-module `#[allow(unsafe_code)]` anywhere — so the safe guarantee is
  tooling-provable (`cargo-geiger` reports zero).

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
this one (see TP-D4 for the thread pool crate, and D16 for the `windows-text` crate).
Each `unsafe` leaf is its **own** `-sys` crate (Option B), so no crate ever mixes
`unsafe` with logic: the live-registry leaf (M5), the `windows-text-sys` leaf, the thread
pool leaf, and any HWC/COM leaf are all separate `-sys` crates.

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

## D16 — `windows-text`: a standalone reusable Windows string crate

The ordinal-casing seam (D6/D8) and the UTF-16 storage / UTF-8 boundary
transcoding (D7/D9) are factored out of this crate into a new standalone sibling
crate, **`windows-text`**. Per Option B (D13) it is split into **two** crates:

- **`windows-text`** — the safe layer, **unconditionally
  `#![forbid(unsafe_code)]`**. Owns the `Utf16` string type (a safe owned
  UTF-16 string shaped after `std::basic_string<char16_t>` — see below), the
  UTF-8↔UTF-16 mapping, the `OrdinalCasing` trait, the `Win32OrdinalCasing`
  production impl (which merely calls the `-sys` crate), and a feature-gated
  pure-Rust ASCII reference impl for downstream off-Windows unit tests.
- **`windows-text-sys`** — the only crate with `unsafe`: the
  **buffer-management-critical** Win32 string primitives, each wrapped as a safe
  slice-in / owned-out fn — `compare_ordinal_ignore_case` / `sort_key`
  (`CompareStringOrdinal` / `LCMapStringEx`) and the code-page transcoders
  (`MultiByteToWideChar` / `WideCharToMultiByte`) — over the `windows` binding
  (D1). Each owns its two-call buffer logic; no pointers escape.

**The `Utf16` type is the safe string layer (shape borrowed from `m::strings` /
`m::utf`).** Mirroring the C++ `m` libraries — which wrap `CompareStringOrdinal`
/ `LCMapStringEx` behind safe `std::basic_string` / `string_view` operations
(`m::strings::ordinal_*`, `m::to_u16string`) so callers never see `Windows.h` —
`Utf16` carries the ordinal operations as **inherent safe methods**
(`compare_ignore_case`, `sort_key`) that delegate to the `-sys` crate. Those
libraries are a **shape reference, not a dependency** (as D11 cites
`windows-registry` / `std::fs`). The `OrdinalCasing` **trait is retained for a
different job**: it is the dependency-injection seam that lets
`windows-platform-isolation` unit-test its tree / decorator logic **off Windows**
by swapping the Win32 impl for the ASCII reference — a seam `m` does not need
because its tests run on Windows.

**Rationale.** Windows ordinal case-insensitive comparison and binary sort-key
generation are broadly useful — any code that must agree with the OS on registry
keys, value names, or case-insensitive NTFS names needs exactly this, with no
dependency on PIL. Isolating it makes it independently reusable, and — with the
Option B split (D13) — concentrates the casing `unsafe` in the tiny
`windows-text-sys` leaf, leaving the safe `windows-text`
crate (and everyone downstream) unconditionally `#![forbid(unsafe_code)]`.

**Charter & scope.** `windows-text` is intended to grow into the Rust home for
much of what the C++ `m` string libraries provide (`m::strings`, `m::utf`,
`m::windows_strings`). Committed scope: the `Utf16` type + UTF-8↔UTF-16 mapping,
ordinal casing (compare + sort key), and **code-page support**
(`MultiByteToWideChar` / `WideCharToMultiByte` over arbitrary code pages —
porting `m::windows_strings::convert`). It **grows incrementally** — split,
view/punning conversions, compare helpers, static strings — as a consumer
actually needs each piece (per "design notes are not a work queue", only the
code-page work is queued now; the rest is charter, not backlog). Explicitly
**out of scope for now**: UTF-32 and other exotic transcoding.

**Naming.** `windows-text`, not `windows-string` / `windows-strings`: the latter
**collides with the published windows-rs `windows-strings` crate** (`HSTRING` /
`BSTR` / `PCWSTR`). `windows-text` signals the higher-level semantics and avoids
the clash.

**Consequence for this crate.** After adopting the dependency (CHECKLIST M3),
this crate re-exports `windows-text`'s `Utf16` / `OrdinalCasing` /
`Win32OrdinalCasing` for API continuity and deletes its own `wstr.rs`
definitions. Because the live-registry leaf is *also* its own `-sys` crate
(Option B, CHECKLIST M5), `windows-platform-isolation` itself contains **no
`unsafe` at all** and is unconditionally `#![forbid(unsafe_code)]`.

**Behavior is owned by us (Design-Autonomy).** Our specification — ordinal
(not linguistic) case-insensitivity per D6, binary sort keys per D8, lossless
UTF-16 with fallible UTF-8 egress per D7/D9 — does not change; only its home
does. `windows` is the chosen binding (D1), confined to the `-sys` leaf.

This decision schedules work: see `CHECKLIST.md` milestones **M2** (build the
`-sys` leaf + safe crate) and **M3** (adopt it here).

## D17 — HWC redirection runs out of process (named-pipe service + async capture pipeline)

**Intent (long-horizon; detailed design deferred).** For the HWC (Hostable Web
Core) surface, the **majority of the redirection work moves out of the hosted
process** into a separate **service-like executable**. The hosted process keeps
only a thin in-process shim and talks to the service over **named pipes** using
a defined protocol. The protocol and journal format are deliberately **left to
be designed later**.

**Capture hot path (thread-frugal by construction).** When capturing traffic to
construct an API design, the in-process shim — *on the intercepted caller
thread* — captures **only the minimum information needed**, then immediately
hands it **off the thread** onto a thread-pool worker via an **MPMC queue**,
releasing the caller thread as fast as possible. The thread-pool worker (M7
substrate) then ships the captured records to the service over **async I/O
(IOCP)**, so threads are tied up in blocking I/O to the least degree possible.
This respects D12: the isolation core stays synchronous; all async lives in the
HWC layer / sibling crates.

**Service responsibilities (choice deferred).** The service either (a) forms the
API model from the journal **dynamically** as records arrive, or (b) simply
**journals** the raw messages and **post-processes** the journal into the API
offline. We choose between these when we get there.

**Out-of-process extension points.** Beyond capture, **API validation** and
**work injection** are also intended to be **injectable from out of process**
across the same service boundary / protocol — not just observation. Recorded now
so the boundary is designed with this in mind; not scheduled in detail.

**FFI / layering.** Named-pipe + IOCP async I/O is FFI; per Option B (D13) it
lives in its own leaf `-sys` crate(s) over the `windows` binding (D1), atop the
M7 threadpool/executor substrate. No `unsafe` leaks into the HWC logic above it.

**Status.** Only a **high-level outline** is queued — see `CHECKLIST.md` **M8**.
Protocol, journal format, API-formation strategy, and the validation/injection
control plane are explicitly **TBD** and will be designed when M8 is scheduled.

## D18 — The shared C++ PIL registry artifact format (D5 read side)

The persisted artifact the Rust loader must read (D5/D15) is **XML produced by
pugixml**, built in **wchar (UTF-16) mode** in this repository. A saved platform
is one document rooted at `<Platform>`; the registry lives under
`<Platform><Registry>`. (The filesystem lives under `<Platform><Filesystem>` —
out of scope until M6. The diagnostic log is a *separate* artifact rooted at
`<DiagnosticLog>` and never appears inside `<Platform>`.) This section documents
the **observed** C++ schema (what the C++ writes and reads); the Rust loader's
mapping decisions are D19.

**Element/attribute schema.**

- `<Registry>` — contains one `<Key>` per predefined hive captured in the
  snapshot.
- `<Key>` (a hive root or a subkey):
  - `name` *(string, required)* — for a hive root, a predefined-hive spelling
    (below); for a subkey, its single path component.
  - `last_write_time` *(signed 64-bit decimal, optional)* —
    `time_point_type::time_since_epoch().count()` (raw clock ticks). Emitted only
    when not `min`; doubles as the C++ load-side version stamp (D5 lazy repair).
  - `deleted="true"` *(optional)* — tombstone: the key was deleted in the
    overlay. Carries no children.
  - `mirrored="true"` *(optional)* — name-only placeholder: the subkey name was
    observed but its contents were never captured. Carries no children. Mutually
    exclusive with `deleted` and with having children.
  - Children — zero or more `<Value>` and nested `<Key>` (only when the key is
    neither `deleted` nor `mirrored`).
- `<Value>`:
  - `name` *(string, required)* — empty name denotes the key's default value.
  - `deleted="true"` *(optional)* — value tombstone; no `type`/`data`.
  - `type` *(decimal `uint32`)* and `data` *(hex string)* — present on every
    non-deleted value. A mirrored value whose bytes were never loaded is **not**
    emitted at all, so a present non-deleted `<Value>` always carries both.

**`type` = `m::pil::reg_value_type` (`registry_base_types.h`), the Win32 `REG_*`
numbering:** `0` none, `1` string (`REG_SZ`), `2` expand_string
(`REG_EXPAND_SZ`), `3` binary (`REG_BINARY`), `4` uint32 (`REG_DWORD`, LE), `5`
uint32_big_endian (`REG_DWORD_BIG_ENDIAN`), `6` link (`REG_LINK`), `7`
multi_string (`REG_MULTI_SZ`), `11` uint64 (`REG_QWORD`, LE).

**`data` encoding.** The **raw OS value bytes** as lowercase hex, two digits per
byte, high nibble first, no separators (`bytes_to_hex` /
`hex_to_bytes`). String types are UTF-16LE bytes (typically NUL-terminated);
`REG_MULTI_SZ` is NUL-separated, double-NUL terminated UTF-16LE; `REG_DWORD` /
`REG_QWORD` are 4/8 little-endian bytes. An odd-length or non-hex `data` is a
hard parse error on the C++ side.

**Predefined-hive `name` spellings.** The C++ **save** side emits the canonical
spellings from `pk_to_string_map`: `HKCR`, `HKCU`, `HKLM`, `HKEY_USERS`,
`HKEY_PERFORMANCE_DATA`, `HKCC`, `HKEY_CURRENT_USER_LOCAL_SETTINGS`,
`HKEY_PERFORMANCE_TEXT`, `HKEY_PERFORMANCE_NLSTEXT`. The **load** side
(`predefined_key_names`) additionally accepts the long forms
`HKEY_CLASSES_ROOT`, `HKEY_CURRENT_USER`, `HKEY_LOCAL_MACHINE`,
`HKEY_CURRENT_CONFIG`, matched **ordinal case-insensitively**. An unrecognized
hive name is a hard error (`invalid_parameter`).

**Worked example.**

```xml
<Platform>
  <Registry>
    <Key name="HKLM">
      <Key name="Software" last_write_time="133600000000000000">
        <Value name="Name" type="1" data="62006100730065000000"/>
        <Value name="Count" type="4" data="18000000"/>
        <Value name="old" deleted="true"/>
        <Key name="App"><Value name="" type="1" data="68006900000000"/></Key>
        <Key name="Observed" mirrored="true"/>
        <Key name="Gone" deleted="true"/>
      </Key>
    </Key>
  </Registry>
</Platform>
```

**Source of truth.** `src/libraries/pil/src/buffered/registry.cpp`
(`registry::save_xml` / `registry::load_xml`),
`src/libraries/pil/src/buffered/registry_key_key_operations.cpp`
(`key::save_xml` / `key::load_children_xml`, `bytes_to_hex` / `hex_to_bytes`),
`src/libraries/pil/src/key_path.cpp` (predefined-hive name maps),
`src/libraries/pil/include/m/pil/registry_base_types.h` (`reg_value_type`). Per
D5 this schema is a **shared** contract: it may be changed only by jointly
migrating both code bases with confirmation, never unilaterally.

## D19 — Rust loader mapping from the C++ registry artifact

How the Rust read side (M4-3) turns a D18 document into in-memory state. The C++
artifact is a *sealed buffered snapshot*; the loader folds it into an immutable
base [`Hive`] that callers wrap in `OverlayTree::new(casing, hive)`. "M1 overlay
tree" in the checklist therefore means **the base of the overlay tree** — the
loader produces the captured base, not a pre-populated overlay.

**Single hive, hive-name first component.** The Rust tree is one `Hive` rooted at
a session-vended root whose first path component is the canonical hive name
(`Session::root` → `WellKnownRoot::canonical_name`, e.g.
`HKEY_LOCAL_MACHINE`). Each C++ `<Key>` under `<Registry>` becomes a **first-level
subkey** of that single `Hive`, named with the **canonical full name**, so paths
vended by a session resolve. Every accepted C++ spelling (abbreviation or long
form, ordinal case-insensitive) normalizes to one canonical name: `HKCR`/
`HKEY_CLASSES_ROOT` → `HKEY_CLASSES_ROOT`; `HKCU` → `HKEY_CURRENT_USER`; `HKLM`
→ `HKEY_LOCAL_MACHINE`; `HKCC`/`HKEY_CURRENT_CONFIG` → `HKEY_CURRENT_CONFIG`;
`HKEY_USERS`, `HKEY_CURRENT_USER_LOCAL_SETTINGS`, `HKEY_PERFORMANCE_DATA`,
`HKEY_PERFORMANCE_TEXT`, `HKEY_PERFORMANCE_NLSTEXT` → themselves. An unrecognized
hive name is a parse error.

**Folding a sealed snapshot into an immutable base.**

- A non-deleted, non-mirrored `<Key>` → a `Hive` subkey; recurse into it.
- A non-deleted `<Value>` → decoded and inserted (below).
- **Tombstones** (`deleted="true"`, key or value) are **skipped**. A sealed
  snapshot is "the world as captured"; a deleted entry is simply absent, and the
  immutable base `Hive` has no tombstone concept (tombstones are overlay/journal
  state). When the Rust write/round-trip side arrives (M5) tombstones become
  overlay deletions; for the M4 read side they fold away.
- A **mirrored placeholder** (`mirrored="true"`) → an **empty** `Hive` subkey, so
  the observed name still enumerates in ordinal order. This is a deliberate
  simplification: the C++ sealed-world "enumerable-but-not-openable, then
  lazy-repaired" nuance is overlay behavior the immutable base does not model in
  M4. The read side asserts enumeration, which an empty key reproduces.
- `last_write_time` is **parsed and ignored** — the M4 base `Hive` models no
  per-key timestamp. (Revisit if key metadata is ever modeled.)
- Absent `<Registry>` → an empty `Hive` (not an error), mirroring the C++
  `if (!registry_node) return reg;`. The loader accepts being handed either the
  `<Platform>` root or a `<Registry>` element directly.

**Value decode (`type` + `data`).** `data` hex → raw bytes (odd-length or non-hex
→ parse error), then by `reg_value_type`:

| `type` | → `ValueData` | decode |
|---|---|---|
| 1 string | `String(Utf16)` | bytes as UTF-16LE units; drop one trailing NUL unit if present |
| 2 expand_string | `ExpandString(Utf16)` | as string |
| 7 multi_string | `MultiString(Vec<Utf16>)` | UTF-16LE units split on NUL; drop trailing empties (double-NUL terminator) |
| 4 uint32 | `Dword(u32)` | exactly 4 little-endian bytes (else parse error) |
| 11 uint64 | `Qword(u64)` | exactly 8 little-endian bytes (else parse error) |
| 3 binary | `Binary(Vec<u8>)` | raw bytes |
| 0 none, 5 dword_be, 6 link, other | `Binary(Vec<u8>)` | raw bytes — lossless fallback; `ValueType` has no variant for these |

A string/multi-string `data` that is not a whole number of `u16` (odd byte count)
is a parse error. Per **D9** the loader never rejects ill-formed UTF-16 string
*content*: decoded units are stored losslessly; only UTF-8 egress is fallible.

**Parser.** roxmltree (pure-safe, read-only DOM) keeps the loader inside
`#![forbid(unsafe_code)]` (D13). Behavior is owned by us (Design-Autonomy):
roxmltree is the chosen mechanism for the XML grammar D18 already specifies.

## Status

The kickoff questions (relationship, surfaces, layering, interop) are resolved
as D2–D5. D6–D9 are **binding string-handling invariants**. D10–D12 settle
provider composition, object shapes, and the threading stance (registry roots
are session-vended). D13 is the unsafe-quarantine invariant; D14 the per-surface
error rule; D15 the no-live-provider first cut; D16 factors the casing seam into
the standalone `windows-text` (+ `-sys` leaf) crates; D17 records the
out-of-process HWC redirection architecture (named-pipe service + async capture
pipeline), deferred in detail; D1/D13 confine
all `unsafe` to leaf `-sys` crates bound to `windows`. Milestone M1 (pure safe core) is
complete; M2+ are queued in `CHECKLIST.md` and cross-referenced to these
D-numbers. The Windows thread pool / async work lives in the sibling
`windows-threadpool` crate (and a future executor crate), not here.
