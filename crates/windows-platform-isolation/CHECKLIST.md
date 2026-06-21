# windows-platform-isolation — CHECKLIST

Action-only checklist. Completed groups move to `COMPLETED-CHECKLIST.md`.
Decision references point at `DESIGN-NOTES.md` (D-numbers).

## Milestone M1 — Pure safe core (no FFI, no persistence)

The entire registry isolation core as safe-half code (D13), unit-testable with
synthetic in-memory data. No `unsafe`, no `windows-sys`, no live/"direct"
provider (D15), no on-disk artifact format (D5 deferred to M2). Every item below
is dependency-ordered; the milestone ends in an integration test.

- [x] **M1-1 — Crate skeleton & module layout.** Replace the default `add()`
  scaffold. Establish the safe module layout reflecting the D13 split: safe
  modules carry `#![forbid(unsafe_code)]` (or module-level `#![deny]`); reserve
  an empty, documented `ffi` module (no code yet) as the future single unsafe
  home. Define the per-surface `RegistryError` enum stub (D14) and a
  `Result<T> = core::result::Result<T, RegistryError>` alias. Crate builds clean
  debug + release.

- [ ] **M1-2 — String / name / path types (D6–D9).** Internal UTF-16LE storage
  (`Vec<u16>`, D7); fallible UTF-8 egress returning a typed error on ill-formed
  UTF-16 (D9); UTF-8 ingress transcoded once. Define the **ordinal-casing seam**
  — a trait providing ordinal case-insensitive comparison (D6) and sort-key
  generation (D8) — with a **test-only** pure-Rust ASCII-ordinal implementation;
  the production Win32-backed impl is explicitly deferred to the FFI milestone
  and MUST NOT ship from here. Unit tests: UTF-8↔UTF-16 round-trip, ill-formed
  UTF-16 rejection, case-insensitive equality and ordering, sort-key stability.

- [ ] **M1-3 — Registry overlay / copy-on-write tree.** In-memory representation
  of keys, values, and value types (D11 `Type` shape), ordered/keyed via the
  M1-2 ordinal-casing seam. Implement overlay + redirect + copy-on-write merge
  semantics over a base layer. Unit tests: read-through to base, write creates
  CoW copy without mutating base, overlay shadows base, merged enumeration is
  ordinal-ordered and deduplicated.

- [ ] **M1-4 — Reified operation model & `Surface` seam (D10).** `Request` /
  `Response` value types modeling registry operations (open, read, write,
  enumerate, delete). `trait Surface { fn invoke(&mut self, req: &Request) ->
  Result<Response>; }`. A concrete in-memory `Surface` backed by the M1-3 tree.
  Unit tests: each `Request` variant round-trips against the tree-backed surface.

- [ ] **M1-5 — Cross-cutting decorators over the seam (D4).** Pass-through and
  buffered decorators implemented once, surface-agnostically, over `Surface`.
  Buffered captures writes in an overlay and applies them on commit; base stays
  untouched until commit. Unit tests: buffered writes invisible to base
  pre-commit, visible post-commit; pass-through is transparent.

- [ ] **M1-6 — Typed registry facade (D11).** A `Registry` surface trait with
  **session-vended roots** (no global `CURRENT_USER` / `LOCAL_MACHINE`
  constants), typed `get_*` / `set_*` accessors, and key/value iterators, all
  lowering into M1-4 `Request`s. Unit tests: facade calls produce the expected
  `Request` sequence and results.

- [ ] **M1-7 — Integration test (tree logic).** Compose a full stack (facade →
  buffered decorator → in-memory tree surface) over a few hundred synthetic
  keys/values. Assert isolation semantics end-to-end: buffered writes leave the
  base tree unmodified, committed writes merge correctly, reads reflect the
  overlay, enumeration is ordinal-ordered. Runs well under the unit-test time
  budget.

## Deferred (not part of M1 — queued as later milestones)

- **M2 — C++ artifact loader (D5 read side / D15 ingress).** Safe deserializer
  that loads state captured by the C++ PIL providers into the M1 tree. Gated on
  documenting the C++ saved-state format (read the C++ serialization code).
  Ends in an interop test loading a real C++-produced artifact.
- **M3 — FFI leaf & live/"direct" provider.** The single `unsafe` module:
  `windows-sys` bindings, RAII handle wrappers, the production Win32 ordinal
  comparator / sort-key (D6/D8), and a live registry provider. The write/capture
  side of the artifact format.
