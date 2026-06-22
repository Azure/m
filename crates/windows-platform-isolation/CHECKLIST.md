# windows-platform-isolation — CHECKLIST

Action-only checklist. Completed groups move to `COMPLETED-CHECKLIST.md`.
Decision references point at `DESIGN-NOTES.md` (D-numbers).

Milestone M1 (pure safe core) is complete — see `COMPLETED-CHECKLIST.md`.

---

## Roadmap (M2+) — DRAFT for tuning

This is a forward plan laid out for review; item wording and ordering are
expected to be tuned before execution. Milestones are dependency-ordered, sized
to ~5 items, and end in an integration test. Sub-steps use decimal notation.

**Cross-crate note (D16 / Option B / D1).** The ordinal-casing + UTF transcoding
seam moves out of this crate into a reusable sibling, split per Option B (D13)
into a safe crate **`windows-text`** (`#![forbid(unsafe_code)]`) and a
tiny unsafe leaf **`windows-text-sys`** (the only `unsafe`, over the
`windows` binding — D1). Every FFI leaf in the effort is its own `-sys` crate, so
after M3+M5 **`windows-platform-isolation` contains no `unsafe` at all**. When M2
is scaffolded, its detailed checklist moves into the `windows-text` crates'
`CHECKLIST.md`; the M2 block here is the placeholder until then.

### M2 — `windows-text` (safe) + `windows-text-sys` (unsafe leaf) ⟶ new crates

The reusable Windows string layer (ordinal casing, UTF-8/UTF-16 mapping, code
pages), useful independent of PIL — the Rust home for much of `m`'s string
libraries (D16 charter). Per Option B (D13) the `unsafe` lives **only** in the
`-sys` leaf; the safe crate is unconditionally `#![forbid(unsafe_code)]`.

- [x] **M2-1** Scaffold `crates/windows-text-sys` (the unsafe leaf):
      `Cargo.toml` (workspace member, edition 2024, MSRV inherited); bind
      `windows` (D1) with the string features. Expose the
      buffer-management-critical Win32 string primitives as **safe**
      slice-in/owned-out fns — `compare_ordinal_ignore_case(&[u16], &[u16]) ->
      Ordering` (`CompareStringOrdinal(bIgnoreCase = TRUE)`, D6),
      `ordinal_upcase(&[u16]) -> Vec<u16>` (`LCMapStringEx` with `LCMAP_UPPERCASE`
      over `LOCALE_NAME_INVARIANT`; the ordinal sort key is this fold serialized
      big-endian in the safe crate — refines D8, see WT-2), and the code-page
      transcoders `mb_to_wide(cp, &[u8]) -> Result<Vec<u16>>` /
      `wide_to_mb(cp, &[u16]) -> Result<Vec<u8>>` (`MultiByteToWideChar` /
      `WideCharToMultiByte`). Each owns its two-call length-probe + Win32 error
      mapping; all `unsafe` confined here.
- [x] **M2-2** Scaffold `crates/windows-text` (the safe crate):
      `#![forbid(unsafe_code)]` with **no** `#[allow]`, README, its own
      `DESIGN-NOTES.md`; depends on the `-sys` leaf. Add both crates to the
      workspace `members` list.
- [x] **M2-3** `Utf16` string type (lifted from this crate's `wstr.rs`): a safe
      owned UTF-16 string shaped after `std::basic_string<char16_t>` — UTF-8
      ingress (`from_utf8`/`From`), lossless `Vec<u16>` storage (D9), fallible
      UTF-8 egress via `char::decode_utf16` (D7/D9). Owns the UTF-8↔UTF-16
      mapping (our own `encode_utf16`/`decode_utf16`, not Win32
      `MultiByteToWideChar`).
- [x] **M2-4** Safe ordinal API: inherent methods `Utf16::compare_ignore_case(&self,
      &Utf16) -> Ordering` (D6) and `Utf16::sort_key(&self) -> Vec<u8>` (D8)
      delegating to the `-sys` leaf (shape borrowed from `m::strings` / `m::utf`).
      Plus the `OrdinalCasing` trait as the off-Windows DI/test seam, with a
      feature-gated (`testing`) pure-Rust ASCII **reference** impl.
- [x] **M2-5** Safe code-page API: a `CodePage` type (CP_ACP, CP_OEMCP, CP_UTF8,
      explicit numeric code pages) plus `Utf16::from_code_page(cp, &[u8])` /
      `to_code_page(cp)` over the `-sys` transcoders, with a typed error for
      invalid sequences (D9). Ports `m::windows_strings::convert`. (UTF-32 /
      exotic transcoding explicitly out of scope — D16.)
- [x] **M2-6** Tests: differential ASCII parity (reference vs Win32) over
      a–z/A–Z/digits/symbols; non-ASCII ordinal cases proving ordinal ≠
      linguistic (e.g. Turkish dotted/dotless I); sort-key equality + ordering
      monotonicity; code-page round-trips (UTF-8 and a legacy CP); ill-formed
      UTF-16 round-trips never panic/lose data (D9). Safe crate stays
      `cargo-geiger`-clean (zero `unsafe`).
- [x] **M2-7** *(integration)* Shared golden sort-key/comparator vectors (WT-5).
      A Windows-only generator (`windows-text` example `gen_ordinal_golden`)
      calls the OS primitives (`LCMapStringEx` under `LOCALE_NAME_INVARIANT` /
      `CompareStringOrdinal`) over a fixed corpus (ASCII
      letters/digits/`_`/punctuation, mixed-case words incl. the `_` / `a_b` vs
      `ab` ordinal-vs-linguistic cases (WT-2), BMP non-ASCII incl.
      U+0130/U+0131, ill-formed UTF-16), emitting per-input key bytes (hex) and
      per-pair comparator signs into a committed fixture
      (`crates/windows-text/testdata/ordinal_golden_vectors.txt`). The capture
      is curated against the written spec: the generator asserts the comparator
      sign agrees with the byte ordering of the two keys for every pair (D8), so
      any OS quirk that diverged would fail generation rather than be committed
      silently. The Rust PIL/`windows-text` suite loads and asserts against the
      file (`win32_matches_golden_vectors`). Removes the old "extract reference
      vectors from C++" prerequisite. The **C++** consumer half is split out to
      M4-6 below, because asserting the C++ side requires the C++ materialized
      sort-key representation to be pinned first — that work lives in M4 (C++
      artifact format), not M2.
      > **➡ CROSS-COMPONENT HANDOFF:** the C++ side of the shared fixture is
      > `windows-platform-isolation` → M4 → **M4-6** (C++ PIL suite loads
      > `crates/windows-text/testdata/ordinal_golden_vectors.txt`).
- [x] **M2-8** Sort key = a `memcmp`-comparable **byte** key built from ordinal
      upper-casing (`LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE)`
      serialized big-endian); comparator = `CompareStringOrdinal(bIgnoreCase =
      TRUE)`. The key and comparator are consistent in **both** equality and
      ordering. `LCMAP_SORTKEY | NORM_IGNORECASE` was considered and rejected
      because its linguistic ordering diverges from the ordinal comparator on
      punctuation (`"a_b"` vs `"ab"`), which would break the `OrdinalCasing`
      contract, D6, the ASCII reference, and ordinal tree iteration. Ordinal
      upper-cased bytes are equally composable while staying ordinal-consistent.
      Tests assert ordering parity (`win32_sort_key_agrees_with_compare`,
      `win32_matches_ascii_reference_on_ascii`). See WT-2; pinned by WT-5.

### M3 — Adopt `windows-text` here (integration / refactor)

- [x] **M3-1** Add the dependency; remove `Utf16` / `OrdinalCasing` / the
      test-only `AsciiOrdinalCasing` from `wstr.rs`; re-export the crate's
      `Utf16`, `OrdinalCasing`, `Win32OrdinalCasing` for API continuity. Retire
      `wstr.rs` once empty. Also removed the now-obsolete empty `ffi` module: per
      Option B (D13) every FFI leaf is its own `-sys` crate, so this crate keeps
      no local unsafe home (tests import `AsciiOrdinalCasing` from `windows-text`
      via its `testing` dev-dependency feature).
- [x] **M3-2** Make `Win32OrdinalCasing` the default casing for non-test builds
      (Windows-only `Registry::in_memory` facade vends it); unit tests use the
      crate's `testing` reference impl.
- [x] **M3-3** *(integration)* Clean rebuild + full test suite in **debug and
      release** + clippy; confirm zero behavior change across
      tree/surface/decorator/registry/integration layers; this crate stays
      genuinely `#![forbid(unsafe_code)]` (casing `unsafe` now lives in the
      `windows-text-sys` leaf).

### M4 — C++ artifact format & loader (D5 read side / D15 ingress)

- [x] **M4-1** Document the C++ PIL saved-state/serialization format (read the
      serialization code under `src/libraries/pil/`). Recorded as **D18** in
      `DESIGN-NOTES.md`: pugixml `<Platform><Registry>` XML — `<Key>`/`<Value>`
      schema, `deleted`/`mirrored` markers, `reg_value_type` `type` numbering,
      lowercase-hex `data`, and predefined-hive name spellings.
- [x] **M4-2** Record the shared-format spec in `DESIGN-NOTES.md` (D5 is a
      prerequisite for the loader). Recorded as **D19**: loader produces a base
      `Hive`, normalizes hive names to canonical forms, folds tombstones away /
      mirrored→empty key, ignores `last_write_time`, and decodes `type`/`data`
      per `reg_value_type` (with a lossless `Binary` fallback); roxmltree keeps
      it `#![forbid(unsafe_code)]`.
- [x] **M4-3** Safe deserializer: bytes → M1 overlay tree, keyed with M2
      production sort keys for C++ parity (D8/D12). No `unsafe`. Added
      `src/serial.rs` — `load_registry_hive(casing, xml) -> Result<Hive>` over
      roxmltree (D19): normalizes hive names, folds tombstones/mirrored, decodes
      `type`/`data`. 18 unit tests; `#![forbid(unsafe_code)]` holds.
- [x] **M4-4** Extend `RegistryError` for parse/format failures as needed (D14).
      Added `RegistryError::MalformedArtifact(String)` (with `Display`) for
      malformed XML / unknown hive name / undecodable value — the loader's
      parse-failure channel. Sequenced before M4-3 (the deserializer needs it).
- [x] **M4-5** *(integration)* Load a real C++-produced artifact; assert tree
      contents and ordinal enumeration order. Added a hand-authored
      spec-conformant fixture `testdata/registry_artifact.xml` (swap a real
      C++ artifact in later) and an integration test that loads it via
      `load_registry_hive` and asserts hive-name normalization, all value-type
      decoding, tombstone/mirrored folding, and ordinal subkey enumeration.
- [x] **M4-6** *(integration, from M2-7)* Have the C++ PIL test suite load the
      shared golden fixture `crates/windows-text/testdata/ordinal_golden_vectors.txt`
      and assert the C++ ordinal sort key (per-`I`-row key bytes) and
      `m::case_insensitive_less` comparator (per-`C`-row sign) reproduce it
      exactly, pinning Rust↔C++ parity of the ordinal key/comparator (D8/D12).
      Added `src/libraries/pil/test/Platforms/Windows/test_ordinal_golden_vectors.cpp`
      (Windows-only) to the `test_win32_registry` suite, with the fixture path
      passed via the `M_ORDINAL_GOLDEN_VECTORS` compile definition. `SortKeyFormatParity`
      reconstructs each `I`-row key from the documented algorithm
      (`LCMapStringEx`/`LCMAP_UPPERCASE` serialized big-endian) and asserts byte
      equality; `ComparatorParity` asserts every `C`-row sign via the production
      `m::case_insensitive_less<std::wstring>` (CompareStringOrdinal). NOTE: the
      C++ PIL stack does not materialize a byte sort key in production (its
      case-insensitive maps key on the comparator directly), so the `I`-row check
      pins the documented sort-key *format* cross-language rather than a
      production C++ materializer.
      > **⬅ CROSS-COMPONENT PREREQUISITE:** the fixture and its Rust consumer
      > landed in M2-7. This item additionally requires the C++ materialized
      > sort-key byte representation pinned by M4-2/M4-3 (so the C++ `I`-row key
      > assertion is well-defined). Sequence after M4-3.

### M5 — Live/"direct" registry provider + capture (write side)

- [x] **M5-1** Scaffold a registry `-sys` leaf crate (Option B): RAII `HKEY`
      wrappers + error mapping over the `windows` binding (D1) — the registry
      `unsafe` leaf, separate from `windows-platform-isolation`, which stays
      unconditionally `#![forbid(unsafe_code)]`. (New crate
      `windows-platform-isolation-sys`: `RegKey` RAII + roots + open/create,
      `RegError`/`is_not_found`; D20.)
- [ ] **M5-2** Live registry `Surface` over the real OS registry (read path).
- [ ] **M5-3** Live write path.
- [ ] **M5-4** Write/capture side of the artifact format (round-trips with M4).
- [ ] **M5-5** *(integration)* capture → save → load → assert round-trip parity
      with the C++ format.

### M6 — Filesystem isolation surface (mirror the registry stack)

- [ ] **M6-1** `FilePath` type (UTF-16/ordinal; NTFS case-insensitivity via M2
      casing) + hand-rolled `FilesystemError` (D14).
- [ ] **M6-2** Overlay / copy-on-write filesystem tree (mirror M1's tree).
- [ ] **M6-3** Reified filesystem `Request`/`Response` + `Surface`; reuse the
      surface-agnostic decorators (D4/D10).
- [ ] **M6-4** Typed filesystem facade (`std::fs`-shaped, D11) + session vending.
- [ ] **M6-5** *(integration)* Load a C++ filesystem artifact; assert contents
      and ordinal directory ordering.

### M7 — Async / threadpool foundation (sibling crates; isolation stays sync, D12) — OUTLINE

- [ ] **M7-1** `windows-threadpool` safe API over `CreateThreadpoolWork`/timers
      (TP-D2), quarantined `unsafe` (TP-D4).
- [ ] **M7-2** `windows-threadpool-executor` crate: futures executor submitting
      threadpool work.
- [ ] **M7-3** IOCP reactor via `CreateThreadpoolIo` for async I/O completion.
- [ ] **M7-4** *(integration)* spawn+await, timer fire, IOCP completion smoke
      tests.

### M8 — HWC (Hostable Web Core) isolation layer — OUTLINE (to be detailed when scheduled)

Majority of redirection runs **out of process** per D17; everything below is a
high-level placeholder — protocol, journal format, and API-formation strategy
are deferred ("design later").

- [ ] **M8-1** HTTP listener / webcore surface shapes (C++
      `http_listener_interfaces.h` / `webcore.h`).
- [ ] **M8-2** Out-of-process **service executable** + thin in-process shim,
      communicating over **named pipes** with a defined protocol (D17).
- [ ] **M8-3** Capture hot path: on the caller thread record the minimum, hand
      off via an **MPMC queue** to a threadpool worker (M7) that ships records to
      the service over **async I/O (IOCP)** (D17).
- [ ] **M8-4** Service side: journal messages, then either form the API model
      dynamically or post-process the journal into the API (choice deferred, D17).
- [ ] **M8-5** Make **API validation** and **work injection** injectable from
      out of process across the same service boundary (D17).
- [ ] **M8-6** Named-pipe + IOCP FFI leaf as its own `-sys` crate (Option B / D13).
- [ ] **M8-7** *(integration)* end-to-end HWC scenario over an isolated world
      (scope TBD — flag for tuning).
