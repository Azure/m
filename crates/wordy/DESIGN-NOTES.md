# wordy — Design Notes

`wordy` is a "shared dictionary" REST service implemented as a Rust IIS native
module. Within this repository it serves two purposes: it is the proof harness
for the link-time host-call redirection built in
[`windows-win32-shim`](../windows-win32-shim) (see that crate's SHIM-D19), and it
is where genuine Hostable Web Core (HWC) business logic is grown. This file
records `wordy`'s own design decisions; the surrounding isolation strategy lives
in the shim's design notes.

## WD-D1 — The shim-unaware contract

`wordy` is **deliberately unaware** of any isolation machinery. This is a
load-bearing property, not an accident:

- `wordy` has **no dependency** on `windows-win32-shim` and references none of its
  types, exports, or concepts.
- `wordy` contains **no isolation code** — no aliasing, no shimming, no `.pilcfg`,
  no awareness that its host calls might be redirected.
- The **only** external artifacts the isolation harness may attach to `wordy` are
  *link inputs* supplied through the environment at build time (see WD-D2). The
  decision to isolate `wordy` is therefore made entirely from the outside, exactly
  as it would be for a third-party application whose source we do not control.

A plain build with no environment variables set produces an ordinary IIS native
module; that plain build *is* the evidence that the crate carries no isolation
knowledge.

## WD-D2 — Generic env-driven `build.rs`

`build.rs` injects extra link inputs **only** when `WORDY_EXTRA_LINK_SEARCH`,
`WORDY_EXTRA_LINK_OBJ`, and/or `WORDY_EXTRA_LINK_LIB` are set, and otherwise does
nothing. These names are generic ("extra link object", "extra link lib"); the
script carries no knowledge of aliases or shims. Build-script directives are used
rather than external `RUSTFLAGS` / `.cargo/config.toml` because they are scoped to
this crate's final artifact and cache deterministically, without leaking link
flags to sibling crates.

## WD-D3 — `wordy` declares its own IIS ABI (peer of `mwinweb`)

The IIS `httpserv.h` native-module interfaces are not part of `windows-sys`, so
the ABI is modeled as hand-rolled `#[repr(C)]` vtables. `wordy` declares its
**own** copy of the minimal subset it uses (`src/iis.rs`) rather than reusing the
shim's `mwinweb` module — reusing `mwinweb` would couple `wordy` to the shim and
break WD-D1. The modest duplication is intentional and acceptable; extracting a
neutral `iis-native-module` crate that both could share is deferred. The modeled
vtable layouts are pinned precisely when a genuine host is bound (MW13-5).

## WD-D4 — `RegisterModule` is exported under its real name

IIS loads `wordy.dll` directly and calls its `RegisterModule` export by name, so
`wordy` exports `RegisterModule` (not the shim's internal `mRegisterModule`). All
`unsafe` lives in `src/iis.rs`, which the crate root opts into explicitly while
denying `unsafe_code` everywhere else.

## WD-D5 — Safe routing core split from the ABI boundary

Request handling is split so the decision logic is testable without a host:
`src/routes.rs` is pure, safe, and platform-independent (method + URL → outcome),
while `src/iis.rs` only decodes the host request into strings and realizes the
outcome against the host response. MW13-1 seeds the dispatcher with a single
health route (`GET /healthz` → 200); later milestones grow it into the full
dictionary surface.

## WD-D6 — Word-domain core (`src/words.rs`)

The word business logic lives in `src/words.rs`, a pure, safe,
platform-independent module with **no IIS, filesystem, or isolation awareness**
(MW13-2). It is the "no-HWC end-goal in miniature": the dictionary behavior runs
and is fully unit-tested with zero host.

**Owned specification (Design Autonomy).** `wordy` defines its dictionary
behavior; `fst` and `regex` are chosen because they satisfy it:

- The shared dictionary is a **case-folded** set: every source entry is
  lowercased on load, so membership, enumeration, and suggestions are
  case-insensitive. (Distinct-case forms such as *March* / *march* collapse —
  acceptable for a spell/word service.)
- **Membership** is exact after folding; **enumeration** anchors the supplied
  `regex` to the whole word (so `cat` matches only `cat`, never `cats`); both
  enumeration and suggestions return results in ascending lexicographic order.
- The **anagram** solver is positional: an `AnagramQuery` carries a `template`
  (its length fixes the candidate length; `Slot::Fixed` positions are free
  givens that must match and do **not** draw from the tray; `Slot::Blank`
  positions must be filled from the tray), a `LetterMultiset` tray, and a count
  of **wildcard** tiles that each cover any one needed letter the tray lacks.
  Feasibility is a per-letter multiset cover plus a wildcard-bounded deficit; a
  word with any non-`a`–`z` character (e.g. a possessive) can never fill a blank
  and is rejected. The `parse` form uses `'.'` for a template blank and passes
  wildcards as a separate count so a tray of real letters is never ambiguous.
- **Suggestions** return every dictionary word within a Levenshtein edit
  distance of the folded query; an empty query yields none.

**Single `fst` store.** Word forms are held once in an `fst::Set`, which serves
membership (`contains`), ordered enumeration (stream + regex test), and
edit-distance suggestions (`Levenshtein` automaton) from one compact, sorted
structure — the reason `fst` was chosen over parallel hash/trie structures. The
set is built once per process via a `LazyLock` singleton
(`Dictionary::shared(Locale::EnUs)`); tests share it, keeping the suite well
under the unit-test time budget.

## WD-D7 — Vendored SCOWL `en-US` list + license

The shared dictionary data is `wordlist/en_US.txt` (one lowercased-on-load word
per line), generated by the SCOWL / ESDB custom-list tool (US spelling, medium
size, diacritics stripped, no special/offensive categories — so the list is pure
ASCII and excludes profanity by default). Its governing copyright and permission
notice travels beside it in `wordlist/COPYING.SCOWL` (the "+ its license file" of
MW13-2); the data is embedded into the binary with `include_str!`. A `Locale`
enum namespaces the data so additional locales drop in without a schema change;
only `EnUs` is populated today.
