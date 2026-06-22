# windows-text — Design Notes

Tier-1 canonical decisions for the `windows-text` (safe) and `windows-text-sys`
(unsafe leaf) crates. The crate charter and the cross-crate decisions that
created these crates live as **D16** (and the `unsafe`-quarantine invariants
D1/D13) in [`../windows-platform-isolation/DESIGN-NOTES.md`](../windows-platform-isolation/DESIGN-NOTES.md);
this file records the decisions local to these two crates.

## Decision index

| ID | Title |
|---|---|
| WT-1 | Two crates: safe `windows-text` + unsafe leaf `windows-text-sys` (Option B) |
| WT-2 | The sort key is an invariant `LCMAP_SORTKEY` byte key; comparator is `CompareStringOrdinal` |
| WT-3 | The leaf exposes the four buffer-critical FFI primitives as safe fns |
| WT-4 | Off-Windows the safe crate compiles to the trait + ASCII reference only |
| WT-5 | Sort-key parity is pinned by shared golden vectors generated from OS APIs |

---

## WT-1 — Two crates: safe `windows-text` + unsafe leaf `windows-text-sys`

Per Option B (D13), `unsafe` is quarantined in a dedicated leaf. `windows-text`
is unconditionally `#![forbid(unsafe_code)]` and depends on `windows-text-sys`
(only under `cfg(windows)`), which holds every `unsafe` Win32 call and binds the
`windows` crate (D1). Consumers see zero `unsafe`; `cargo-geiger` over the safe
crate is zero.

## WT-2 — The sort key is an invariant LCMAP_SORTKEY byte key

**Decision.** Two distinct primitives back the casing seam:

- `compare_ignore_case(a, b)` calls `CompareStringOrdinal(a, b, bIgnoreCase = TRUE)`
  (D6) — an ordinal, case-insensitive comparison over code units.
- `sort_key(s)` calls `LCMapStringEx(LOCALE_NAME_INVARIANT,
  LCMAP_SORTKEY | NORM_IGNORECASE, s)` and returns the raw **byte** key the OS
  produces.

**Key contract (equality, not ordering).** The two primitives share *equality*
but not *ordering*: two `sort_key` results are byte-equal exactly when
`compare_ignore_case` reports `Equal`, so the key is a faithful
case-insensitive identity for hashed/equality use and for compound keys. Their
*orderings* differ — `CompareStringOrdinal` orders by code unit, while an
`LCMAP_SORTKEY` key orders by invariant linguistic collation, which weights
punctuation differently. Concretely, ordinally `"a_b" < "ab"` (`_` = U+005F <
`b`), but the linguistic key orders `"ab" < "a_b"`. A consumer that needs a
single self-consistent *ordering* must pick one primitive as authoritative for
that purpose; the two are interchangeable only for equality.

**Why the byte key.** `LCMAP_SORTKEY` keys *compose*: concatenating the keys of
several fields (with a separator) yields one `memcmp`-comparable key for a
compound (multi-field) ordered map, which a per-code-unit upper-case fold cannot
do. That composability is the reason to materialize a key at all — for a single
field the pairwise comparator alone would suffice.

**FFI note.** `LCMAP_SORTKEY` writes a byte array and counts `cchDest` in
**bytes**, but the `windows` binding types the destination as `&mut [u16]` and
passes its element count. A `[u16; needed]` buffer therefore advertises `needed`
bytes of capacity (it physically holds twice that), which is always sufficient;
the bytes are read back in native memory order. This subtlety is confined to the
`windows-text-sys` leaf (WT-3).

**Owned behavior (Design-Autonomy).** Our specification is "an invariant,
case-insensitive, byte-comparable, composable sort key, paired with an ordinal
case-insensitive comparator." `LCMapStringEx`/`CompareStringOrdinal` are the
chosen mechanisms; the committed golden vectors (WT-5) are the written contract
that pins the exact bytes and comparator signs for both the Rust and the C++
bindings.

## WT-3 — The leaf exposes the four buffer-critical FFI primitives as safe fns

`windows-text-sys` wraps exactly the buffer-management-critical Win32 string
primitives — `CompareStringOrdinal`, `LCMapStringEx` (used for
`LCMAP_SORTKEY | NORM_IGNORECASE`, per WT-2), `MultiByteToWideChar`, and
`WideCharToMultiByte` — each as a safe slice-in / owned-out function that owns
its two-call length probe and `GetLastError` mapping. No raw pointers cross the
boundary.

`compare_ordinal_ignore_case` and `sort_key` are infallible in their public
signatures: empty inputs are handled without calling Win32, and for valid
non-empty inputs the calls cannot fail, so a failure is treated as an
unreachable invariant violation (panic). The code-page transcoders genuinely
fail on invalid sequences and return a `Win32Error`. UTF-8 decoding sets
`MB_ERR_INVALID_CHARS` so malformed UTF-8 is rejected rather than silently
substituted.

## WT-4 — Off-Windows the safe crate compiles to the trait + ASCII reference only

`Win32OrdinalCasing`, the inherent `Utf16` ordinal methods, and `CodePage` are
`cfg(windows)`. Off Windows, `windows-text` still provides `Utf16`, the
`OrdinalCasing` trait, and (under `test` / the `testing` feature) the
`AsciiOrdinalCasing` reference — the dependency-injection seam that lets
downstream crates unit-test case-insensitive logic without Windows (D16).

## WT-5 — Sort-key parity is pinned by shared golden vectors generated from OS APIs

**Decision.** The authoritative definition of the ordinal sort key and the
case-insensitive comparator is a committed **golden-vector fixture**, generated
once from the Windows OS APIs and curated to the behavior we specify, then
consumed as the single source of truth by **both** the Rust tests (here) and the
C++ PIL tests. Neither language's runtime implementation is the oracle for the
other; the committed fixture is (Design-Autonomy: we own the spec, the OS is the
chosen mechanism, the fixture is the written-down contract).

**Why.** It dissolves the cross-component ordering dependency that otherwise
blocks M2-7: today there is no C++ sort-key routine to extract reference vectors
from, so a "match the C++ side" test is unsatisfiable. Generating the vectors
from the OS APIs removes that prerequisite — neither side has to exist before the
other — and makes the contract explicit rather than "whatever the other binding
happens to compute."

**Plan (generate → curate → commit → consume).**
1. *Generate.* A small Windows-only generator calls the OS primitives
   (`CompareString*`/`LCMapStringEx` under `LOCALE_NAME_INVARIANT`) over a fixed
   input corpus — ASCII letters/digits/`_`/punctuation, mixed-case words, BMP
   non-ASCII (e.g. U+0130/U+0131), and ill-formed UTF-16 — emitting, per input,
   the raw key bytes (hex) and, per ordered pair, the comparator sign.
2. *Curate.* The captured output is reviewed against our written specification
   rather than blessed blindly: each row is asserted to match the intended
   behavior, and any OS quirk we choose **not** to adopt is annotated, so the
   fixture encodes "what should ideally happen," not merely "what the OS did."
3. *Commit.* The curated fixture is a checked-in data file (small, stable;
   under the integration-test data conventions) referenced from both test
   suites — the shared artifact, owned here.
4. *Consume.* The Rust tests load the fixture and assert the leaf's key bytes and
   comparator signs match it exactly; the C++ PIL tests load the same file and
   assert the same. Divergence on either side is a failing test pinned to a
   specific row.

The corpus must include the punctuation cases (`_`, `a_b` vs `ab`) that expose
the ordinal-vs-invariant divergence noted in WT-2, because resolving WT-2's open
ordering question (M2-8) is precisely what these vectors adjudicate.
