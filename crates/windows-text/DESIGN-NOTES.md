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
| WT-2 | The sort key is **ordinal**, built from ordinal upper-case (refines D8) |
| WT-3 | The leaf exposes the four buffer-critical FFI primitives as safe fns |
| WT-4 | Off-Windows the safe crate compiles to the trait + ASCII reference only |

---

## WT-1 — Two crates: safe `windows-text` + unsafe leaf `windows-text-sys`

Per Option B (D13), `unsafe` is quarantined in a dedicated leaf. `windows-text`
is unconditionally `#![forbid(unsafe_code)]` and depends on `windows-text-sys`
(only under `cfg(windows)`), which holds every `unsafe` Win32 call and binds the
`windows` crate (D1). Consumers see zero `unsafe`; `cargo-geiger` over the safe
crate is zero.

## WT-2 — The sort key is ordinal, built from ordinal upper-case

**Decision.** `sort_key(s)` ordinally upper-cases the UTF-16 code units via
`LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE)` and serializes the result
big-endian. A plain byte comparison of two keys reproduces the equality and
ordering of `compare_ignore_case` (which calls `CompareStringOrdinal` with
`bIgnoreCase = TRUE`, D6).

**Why this refines D8.** D8 originally described the sort key as `LCMapStringEx`
with `LCMAP_SORTKEY | NORM_IGNORECASE`. `LCMAP_SORTKEY` produces a **linguistic**
collation key, whose ordering diverges from ordinal comparison for realistic
inputs (digits, `_`, and other punctuation relative to letters). But the
consuming tree (`windows-platform-isolation/src/tree.rs`) is specified to iterate
in **ordinal** order, and the `OrdinalCasing` contract requires the sort key to
agree with the ordinal comparator. A linguistic key cannot satisfy both. We
therefore build an **ordinal** key (upper-case fold + big-endian code units),
which is consistent with `CompareStringOrdinal` by construction and is a strict
generalization of the ASCII reference implementation.

**Owned behavior (Design-Autonomy).** Our specification is "an ordinal binary key
that reproduces `CompareStringOrdinal(bIgnoreCase)` ordering." We achieve the
fold with `LCMapStringEx`/`LCMAP_UPPERCASE` over the invariant locale because its
per-code-unit upper-casing matches `CompareStringOrdinal`'s fold for ASCII and
the common BMP range. The two folds could in principle differ for exotic code
units; establishing full parity (and, if needed, replacing the fold with a
hand-rolled ordinal upcase table) is deferred to the C++ golden-vector work
(CHECKLIST M2-7 / M4). There is no C++ sort-key implementation today, so no
external parity constraint is broken by this choice.

## WT-3 — The leaf exposes the four buffer-critical FFI primitives as safe fns

`windows-text-sys` wraps exactly the buffer-management-critical Win32 string
primitives — `CompareStringOrdinal`, `LCMapStringEx` (used for `LCMAP_UPPERCASE`,
per WT-2), `MultiByteToWideChar`, and `WideCharToMultiByte` — each as a safe
slice-in / owned-out function that owns its two-call length probe and
`GetLastError` mapping. No raw pointers cross the boundary.

`compare_ordinal_ignore_case` and `ordinal_upcase` are infallible in their public
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
