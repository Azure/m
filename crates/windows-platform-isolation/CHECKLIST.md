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
- [x] **M5-2** Live registry `Surface` over the real OS registry (read path).
      (`windows-platform-isolation-sys` gains `query_value`/`enum_subkey_names`/
      `enum_values` read FFI; new `#[cfg(windows)] live::LiveRegistry` exposes
      `key_exists`/`read_value`/`enum_keys`/`enum_values` inherent readers that
      decode via the shared `serial::decode_value`; `RegistryError::Os` added.
      The full object-safe `impl Surface` lands in M5-3 with the write verbs,
      since the trait needs all eight verbs to compile — the read/write split is
      coupled at the trait boundary, so M5-2 ships inherent readers only.)
- [x] **M5-3** Live write path.
      (`windows-platform-isolation-sys` gains `set_value`/`delete_value`/
      `delete_subkey_tree` write FFI; `serial::encode_value` is the exact inverse
      of `decode_value`; `LiveRegistry` gains `create_key`/`delete_key`/
      `write_value`/`delete_value` and now implements the full eight-verb
      `Surface`, so it is a drop-in provider for the `Registry` facade. Tested
      against a deterministic HKCU scratch subtree with RAII cleanup.)
- [x] **M5-4** Write/capture side of the artifact format (round-trips with M4).
      (`serial::save_registry_hive` is the pure, platform-independent inverse of
      the D19 loader: deterministic ordinal-sorted XML using the shared
      `encode_value`/lowercase-hex codec, so `load`→`save`→`load` is a fixed
      point — proven against both the SAMPLE and the M4 fixture artifact. D21.)
- [x] **M5-5** *(integration)* capture → save → load → assert round-trip parity
      (`LiveRegistry::capture` snapshots a live HKCU scratch subtree into a base
      hive; the Windows-only integration test writes a known subtree, captures →
      `save_registry_hive` → `load_registry_hive`, asserts every value type reads
      back identically and that re-serialization is a fixed point, with RAII
      scratch cleanup. D20/D21.)
      with the C++ format.

### M6 — Filesystem isolation surface (mirror the registry stack)

Path model is a faithful port of the C++ `m::pil::file_path` (D22); the C++
`test_file_path.cpp` is the conformance spec. The C++ split into root parsing
(`M-FS-PATH-1`) and path algebra (`M-FS-PATH-2`) is mirrored here as M6-1/M6-2.

- [x] **M6-1** `FilePath` + `FileRoot` types with `parse_root` (the seven-way
      `FileRootKind`: none/posix/drive/unc/device/extended/extended_unc), lossless
      UTF-16 storage that round-trips through `native()`, and root accessors
      (`root`/`root_kind`/`relative_path`/`is_absolute`/`has_root`); `FileRoot`
      exposes `kind`/`text`/`is_none`/`suppresses_normalization`/
      `is_fully_qualified`. Plus the hand-rolled `FilesystemError` (D14). (D22)
- [x] **M6-2** Path algebra over a `PathSurface { Windows, Posix }` seam:
      `lexically_normal` (separator normalization, `.`/`..` resolution, `..`
      underflow rejected, extended-length verbatim), `split_parent_path_and_leaf_name`
      / `parent_path` / `has_parent_path`, the join `operator/`, and ordinal
      `equivalent` / `precedes` routed through the M2 `OrdinalCasing` seam
      (Windows ordinal-insensitive, POSIX ordinal-sensitive). (D22/D6)
- [x] **M6-3** Overlay / copy-on-write filesystem tree (mirror M1's tree).
      `FileTree` (immutable base) + `OverlayFileTree<C>` (tombstoned COW
      overlay) over a **unified** dir/file namespace (D13) keyed by the ordinal
      sort key (D6/D8); files are **metadata-only** (`FileMetadata`, D14). Paths
      decompose via `FilePath::components()`. `dir_exists` / `file_exists`,
      `create_dir` / `remove_dir`, `set_file` / `remove_file` / `file_metadata`,
      and ordinal-ordered `read_dir` (`DirEntry { name, kind, metadata }`).
- [x] **M6-4** Reified filesystem `FsRequest`/`FsResponse` + `FsSurface` seam
      (D10) with a `TreeFsSurface<C>` leaf provider over `OverlayFileTree`, plus
      a surface-specific `FsPassThrough` decorator. Note: D10 states pass-through
      and buffered "carry surface-specific semantics," so the filesystem mirrors
      the registry's pass-through rather than literally reusing it (the C++ PIL
      likewise ships separate filesystem facets). The genuinely surface-agnostic
      decorators (logging/journaling/fault-injection) are not yet built for
      either surface and remain future work. (D4/D10)
- [x] **M6-5** Typed filesystem facade (`std::fs`-shaped, D11) + session vending.
- [x] **M6-6** *(integration)* Load a C++ filesystem artifact; assert contents
      and ordinal directory ordering.


### M9 — Live/"direct" filesystem provider (write side; mirrors M5)

Adds a live OS filesystem provider so the `FsSurface` stack can run against the
real filesystem (parallel to M5's `LiveRegistry`). Per the layering discussion,
the provider is built on a **safe Win32 file-handle leaf** in
`windows-platform-isolation-sys` over the `windows` binding (mirroring `RegKey`),
**not** `std::fs`: namespace/metadata ops use path-based Win32, and the leaf owns
a RAII file `HANDLE` that — unlike `RegKey` — it **exposes** (`AsRawHandle` /
`AsHandle`) so a future stream/content layer can drive true overlapped I/O
through the M7 `CreateThreadpoolIo` reactor ("threadless" async) or another
engine. The leaf takes **no** dependency on the threadpool; async is composed one
layer up. File **content** stays out of scope for M9 (metadata-only model); the
handle foundation just makes the later stream work a pure addition. The safe
`windows-platform-isolation` crate stays `#![forbid(unsafe_code)]` — all `unsafe`
is confined to the leaf. Dependency-independent of M7/M8; it extends M5/M6 and is
the cross-component prerequisite for the `windows-win32-shim` crate's filesystem
ABI.

- [x] **M9-1** Add a safe Win32 **file leaf** to `windows-platform-isolation-sys`
      over the `windows` binding (D1/D13/D20): a RAII `FileHandle` over
      `CreateFileW` (closes on drop, exposes the raw handle via
      `AsRawHandle`/`AsHandle` for async), `GetFileInformationByHandle` /
      `GetFileAttributesExW` metadata reads, `SetFileAttributesW` + `SetFileTime`
      metadata writes, path-based `CreateDirectoryW` / `RemoveDirectoryW` /
      `DeleteFileW`, and a RAII find-enumeration handle
      (`FindFirstFileW`/`FindNextFileW`/`FindClose`); plus a `FsError(u32)`
      mirroring `RegError`. All `unsafe` confined to the leaf.
- [x] **M9-2** `FilesystemError::Os(u32)` (mirror `RegistryError::Os`) + a live
      read path: `#[cfg(windows)] live_fs::LiveFilesystem` exposing
      `dir_exists`/`file_exists`/`metadata`/`read_dir` over the **M9-1 file leaf**
      (attributes/timestamps/size), decoding into `FileMetadata` and
      ordinal-ordered `DirEntry`s.
- [x] **M9-3** Live write path: `create_dir`/`remove_dir`/`write_file` (create +
      apply `FileMetadata` attributes/timestamps via the M9-1 leaf
      primitives)/`remove_file`, plus the full object-safe `impl FsSurface`, so
      `LiveFilesystem` is a drop-in provider for the `Filesystem` facade.
- [x] **M9-4** `LiveFilesystem::capture`: snapshot a real directory subtree into
      a base `FileTree` (mirror `LiveRegistry::capture`), metadata-only.
- [x] **M9-5** *(integration)* Windows-only: create a deterministic scratch temp
      subtree, drive create/metadata/enumerate/remove through `LiveFilesystem`,
      assert metadata + ordinal `read_dir` parity, with RAII cleanup.
      > **➡ CROSS-COMPONENT HANDOFF:** unblocks `windows-win32-shim` → MW3
      > (filesystem C ABI passthrough). See
      > [`crates/windows-win32-shim/CHECKLIST.md`](../windows-win32-shim/CHECKLIST.md).


### M10 — Short-name (8.3 / `cAlternateFileName`) fidelity for live enumeration (D23)

The live `read_dir` reads a real `WIN32_FIND_DATAW` whose `cAlternateFileName`
the OS populates, but the sys leaf discards it — so a `windows-win32-shim` client
sees 8.3 short names **dropped** where a direct `FindFirstFile*` would pass them
through (a fidelity regression on any `8dot3name`-enabled volume, including the
system volume by default). Restore fidelity by carrying the short name through
the sys leaf and `DirEntry`; synthetic and C++-artifact surfaces default to none.

- [x] **M10-1** sys leaf (`file.rs`): capture `data.cAlternateFileName` into a
      new `FindEntry.alternate_name: Vec<u16>` (empty when the OS supplies no
      short name) via the existing `find_name` helper, in both the
      `FindFirstFileW` and `FindNextFileW` arms of `read_directory`. Unit test:
      enumerate a deterministic scratch dir and assert each `alternate_name`
      equals what a direct `FindFirstFileW` returns for the same child (both
      empty, or both equal — reproducible regardless of the volume's
      `8dot3name` policy).
- [x] **M10-2** safe crate: add `short_name: Option<Utf16>` to `DirEntry`
      (`None` when absent). `live_fs::read_dir` maps `alternate_name`
      (empty ⇒ `None`). Synthetic base nodes (`FileTree` / `OverlayFileTree`)
      gain an optional per-node short name (default `None`) so the field
      round-trips deterministically off-Windows; the C++ artifact load path
      leaves it `None` (out of scope for the artifact schema). Record **D23** in
      `DESIGN-NOTES.md` + decision index; update the M6-3 / `read_dir` doc
      references to the new `DirEntry` shape.
- [x] **M10-3** *(integration)* Deterministic: a synthetic tree surface whose
      entry carries a short name surfaces it via `read_dir`; a no-short-name
      entry yields `None`. Windows-only live check: enumerate a scratch subtree
      through `LiveFilesystem` and assert each `short_name` matches a direct
      `FindFirstFileW` `cAlternateFileName` for the same child.
      > **➡ CROSS-COMPONENT HANDOFF:** consumed by `windows-win32-shim` → MW8
      > (`fill_find_data` emits `cAlternateFileName` for `FindExInfoStandard`).
      > See [`crates/windows-win32-shim/CHECKLIST.md`](../windows-win32-shim/CHECKLIST.md).


### M7 — Async / threadpool foundation (sibling crates; isolation stays sync, D12) — DETAILED

The detailed, authoritative execution plan now lives in the sibling crate:
`crates/windows-threadpool/CHECKLIST.md` (TP-D1..TP-D4). The items below are the
roadmap outline; track and check off the real work there.

- [x] **M7-1** `windows-threadpool` safe API over `CreateThreadpoolWork`/timers
      (TP-D2), quarantined `unsafe` (TP-D4).
- [x] **M7-2** `windows-threadpool-executor` crate: futures executor submitting
      threadpool work.
- [x] **M7-3** IOCP reactor via `CreateThreadpoolIo` for async I/O completion.
- [x] **M7-4** *(integration)* spawn+await, timer fire, IOCP completion smoke
      tests.

### M8 — Web request/response handler surface (safe; in-process pass-through bootstrap) — DETAILED

Supersedes the former out-of-process HWC pipeline outline per D17's "Generalized
by D24–D29" note: the inbound web surface is now reached **in-process** via the
aliasobj + loader-shim technique (D24/D26), and the redirection logic is just
another decorated surface in the D4 stack (D25: off = identity). This milestone
builds the **safe** side — the handler surface and its identity/journaling
decorators — with no `unsafe` and no real web server. The ABI that loads us into
the host and drives the response path is the shim's job (handoff below). The
out-of-process service variant (former M8-2..M8-6) is deferred to D17 as a future
heavy-traffic optimization, not the scheduled entry path.

- [x] **M8-1** Define the safe `RequestHandler` surface: a trait modeling one
      request/response exchange (and/or the notification points
      `on_begin_request` / `on_send_response`) in terms of borrowed request /
      response models, independent of any Windows or COM type.
- [x] **M8-2** `IdentityHandler` decorator — a **true pass-through** that forwards
      to the inner handler and returns its disposition unchanged (D25 "off" =
      identity; the "no behavior change today" endpoint).
- [x] **M8-3** `JournalingHandler` decorator — records each exchange to an
      observation sink (D28 PII-first, D29 volume policy) then forwards unchanged;
      slots into the D4 decorator stack alongside the reg/fs decorators.
- [x] **M8-4** Session wiring: the session selects identity / journaling /
      (future) substituting handler by mode (D25), exactly as the reg/fs backings
      are composed; off yields the bit-identical pass-through.
- [x] **M8-5** *(integration)* Pure-Rust host-emulating harness: drive a synthetic
      request through the decorator stack with a stub inner handler; assert the
      identity path is byte-identical to the undecorated path and the journaling
      path observes the exchange without altering the response.
      > **➡ CROSS-COMPONENT HANDOFF:** the ABI that loads this surface into a real
      > web host and drives the response path is `windows-win32-shim` → **MW11**
      > (module bootstrap + activation-seam interception) → **MW12** (per-request
      > vtable bridge + pass-through wiring + integration). See
      > [`crates/windows-win32-shim/CHECKLIST.md`](../windows-win32-shim/CHECKLIST.md).

### M11 — Egress (network-client) isolation surface (realizes D27; D31)

The outbound network surface — the realization of **D27** ("network is the
majority of the surface"). A safe `EgressSurface` (whole request/response values)
plus the **D25** mode stack as decorators (passthrough / redirect / buffer /
replay / block / observe), with a `LiveEgress` WinHTTP bottom in its own `unsafe`
leaf (D1/D13). Reassembly of the multi-call WinHTTP handle lifecycle into one
`EgressRequest` is the **shim's** job (MW17), not this surface's. First pass is
HTTP only; SOAP/WWSAPI is reserved. See the design session
`design-sessions/DESIGN-SESSION-2026-06-25-egress-surface-and-validation-tier.md`.

- [x] **M11-1** `EgressRequest` / `EgressResponse` value model + `EgressTransport`
      (`Http`; `Soap` reserved) + `EgressError` (one hand-rolled error type, D14)
      + the `EgressSurface` trait (`send(&mut, &EgressRequest) -> EgressResult<
      EgressResponse>`). Pure, `#![forbid(unsafe_code)]`; unit-tested data types.
      *(`src/egress_error.rs` (`EgressError`/`EgressResult`) + `src/egress.rs`
      (`Scheme`, `EgressTransport`, `EgressRequest` with `http()`/`authority()`/
      `is_safe_verb()`/`is_mutating()`, `EgressResponse` with `new()`/`empty()`,
      object-safe `EgressSurface`); re-exported from `lib.rs`. 12 unit tests
      (verb classification incl. ASCII-case + ill-formed UTF-16, authority,
      object-safe trait dispatch + error propagation). 240 tests pass.)*
- [ ] **M11-2** Pure decorators over an in-memory inner: `RedirectingEgress<S>`
      (rule-based scheme/host/port/path rewrite, then delegate),
      `ObservingEgress<S>` (record `(verb, host, path, status)` to the D29 sink,
      PII-first per D28, then forward), `BlockingEgress` (deny with a synthetic
      error). Unit-tested.
- [ ] **M11-3** `BufferedEgress<S>` — capture mutating requests (non-idempotent
      verbs) in an in-memory journal and return a synthetic ack; idempotent reads
      optionally read through to the inner. `journal()` / `commit()` / `rollback()`
      mirror the registry/`FsBuffered` write-buffer (D30). Unit-tested (mutations
      captured; inner untouched; read-your-writes where configured).
- [ ] **M11-4** `ReplayEgress<S>` — serve canned `EgressResponse`s from a preloaded
      fixture set keyed by `(verb, path[, query])` (the "system state pre-loaded"
      mode, D15 ingress); miss policy = block or read-through. Fixtures load from an
      artifact (shared on-disk format, owned here per Design Autonomy). Unit-tested.
- [ ] **M11-5** `LiveEgress` over a dedicated `windows-*-sys` `unsafe` leaf
      (cfg windows): perform one real WinHTTP transaction from an `EgressRequest`
      (`WinHttpOpen`/`Connect`/`OpenRequest`/`AddRequestHeaders`/`SendRequest`/
      `ReceiveResponse`/`QueryHeaders`/`QueryDataAvailable`/`ReadData`/`CloseHandle`).
      All `unsafe` confined to the leaf; the safe crate stays `forbid(unsafe)`.
      *(integration)* gated localhost round-trip (skips when unbindable).
- [ ] **M11-6** *(integration)* `Egress` facade + composition test: drive a request
      through a `Redirecting`→`Observing`→(in-memory inner) stack and a
      `Replay`-over-`Blocking` stack; assert redirect rewrites the target, observe
      records without altering, buffer isolates mutations, replay serves the
      fixture, and passthrough is byte-identical to the bare inner.
      > **➡ CROSS-COMPONENT HANDOFF:** the WinHTTP handle-lifecycle reassembly, the
      > `.pilcfg` `egress` section, and the link-time alias of `winhttp.dll` are
      > `windows-win32-shim` → **MW17**. See
      > [`crates/windows-win32-shim/CHECKLIST.md`](../windows-win32-shim/CHECKLIST.md).
