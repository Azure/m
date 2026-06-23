# windows-win32-shim — COMPLETED-CHECKLIST

Append-only archive of completed CHECKLIST groups. Newest at the bottom.

---

## Moved 2026-06-02 — MW8: `FindFirstFileEx` family completeness (wildcards, search ops, 8.3 short-name passthrough)

Closed the long-deferred SHIM-D12 over-match gap (the find core captured **all**
children and ignored the pattern leaf) and added the extended `Ex` / `Transacted`
enumeration entry points. Behavior owned by us (Design Autonomy): we **specify**
Win32 `FsRtlIsNameInExpression`-style DOS wildcard semantics; the matcher is an
ordinal string primitive in `windows-text` (`WT-6`,
`name_matches_expression`). 8.3 short names ride on `DirEntry.short_name`
sourced from `windows-platform-isolation` → M10. Recorded as **SHIM-D14**.

- [x] **MW8-1** Extend `fs_ops::find_first` to apply a search predicate after
      `read_dir`: a `SearchPredicate { pattern_leaf: Utf16, op: SearchOp,
      case_sensitive: bool }` where `SearchOp` is a named enum
      (`NameMatch` / `LimitToDirectories`) — no manifest numeric constants.
      `NameMatch` keeps entries whose `name` matches the leaf via the
      `windows-text` matcher (`WT-6`); `LimitToDirectories` additionally requires
      `kind == NodeKind::Directory`. Store the predicate in
      `FindEnumerationState` so `find_next` keeps the same filter for the whole
      enumeration. Route the existing `mFindFirstFileW` through this with
      `{ op: NameMatch, case_sensitive: false }` (fixes its over-match). Empty
      result ⇒ `Ok(None)` ⇒ `ERROR_FILE_NOT_FOUND`. Update SHIM-D12 to record
      that wildcard filtering now applies, delegating the matcher spec to `WT-6`.
- [x] **MW8-2** Add the `mFindFirstFileExW` entry point in `mwinfile.rs`:
      validate `fInfoLevelId` (`FindExInfoStandard` / `FindExInfoBasic` ok;
      `FindExInfoMaxInfoLevel` ⇒ `ERROR_INVALID_PARAMETER`), `fSearchOp`
      (`FindExSearchNameMatch` ⇒ `NameMatch`; `FindExSearchLimitToDirectories`
      ⇒ `LimitToDirectories`; `FindExSearchLimitToDevices` /
      `FindExSearchMaxSearchOp` ⇒ `ERROR_INVALID_PARAMETER`) and
      `dwAdditionalFlags` (`FIND_FIRST_EX_CASE_SENSITIVE` honored;
      `FIND_FIRST_EX_LARGE_FETCH` / `FIND_FIRST_EX_ON_DISK_ENTRIES_ONLY`
      accepted-and-ignored; unknown bits ⇒ `ERROR_INVALID_PARAMETER`).
      `lpSearchFilter` must be null (Win32 reserves it). Map to the predicate,
      delegate to `fs_ops::find_first`, fill `WIN32_FIND_DATAW` via
      `fill_find_data` — including the 8.3 short name: emit
      `DirEntry.short_name` into `cAlternateFileName` (truncate + NUL) for
      `FindExInfoStandard`, and **suppress** it (leave empty) for
      `FindExInfoBasic`, matching Win32. The plain `mFindFirstFileW` path emits
      it too (same `fill_find_data`). Record **SHIM-D14** in `DESIGN-NOTES.md`:
      the search-op / info-level / flag mapping, and the short-name passthrough
      (sourced from isolation M10) + Basic-suppression rule. Add
      `mFindFirstFileTransactedW` (same extended shape per the C++ `.def`;
      ignore the transaction handle, matching the C++ forwarding stub).
- [x] **MW8-3** *(integration)* find-Ex behavior tests. Realization split across
      two reachable layers because the W entry points resolve their filesystem
      through a process-global live-OS `ShimSession` singleton with no
      test-injection seam — so they cannot be driven over an in-memory fixture.
      The isolation guarantee is therefore structural: the engine tests run on a
      `TreeFsSurface<Win32OrdinalCasing>` `Filesystem::in_memory`, never touching
      a live-OS path.
      - **Integration (`tests/filesystem.rs`)** over `fs_ops::find_first` /
        `find_next`: wildcard `*.txt` subset, `?` single-char, literal exact,
        `LimitToDirectories` (files excluded), case-sensitive vs default casing,
        no-match ⇒ `Ok(None)` (the W wrapper maps this to `ERROR_FILE_NOT_FOUND`
        + `INVALID_HANDLE_VALUE`), and 8.3 short-name + `emit_short_name`
        propagation across the whole `find_next` walk (Basic suppresses, Standard
        emits) — confirming the stored predicate/flag is honored for the
        enumeration.
      - **Unit (`mwinfile.rs`)** over the private `map_find_ex_params` and
        `fill_find_data`: accepts Standard (emit) / Basic (suppress) /
        `LimitToDirectories` / case-sensitive / accepted-and-ignored perf flags;
        rejects non-null filter, `FindExInfoMaxInfoLevel`,
        `FindExSearchLimitToDevices` / `FindExSearchMaxSearchOp`, and unknown
        flag bits — all ⇒ `ERROR_INVALID_PARAMETER`; and `fill_find_data` emits /
        suppresses / leaves-empty `cAlternateFileName` per info level.


---

## Moved 2026-06-23 — MW5: Link-time Win32→`m` alias (NDJSON manifest, pure-Rust COFF emitter, link-proof)

Unmodified clients that call genuine `RegOpenKeyExW` / `CreateFileW` are
redirected into the shim at link time by defining the `__imp_<Name>` IAT slots to
point at the `m<Name>` exports (plus `/alternatename` fallbacks). Realized twice
on execution: first as a `.def` + C++-text generator (`alias_gen`), then as a
checked-in NDJSON manifest driving a **pure-Rust COFF emitter** (`alias_obj`) that
writes the alias object's bytes directly via the `object` crate — **no C++
compiler and no MSVC tool** produce the artifact, only the client's own linker
consumes it. The two manifests are kept in lockstep by a drift-guard unit test.
The cross-toolchain link-proof (MW5-6) reuses the C++ tree's link-proof program
and confirms redirection end to end. Recorded as **SHIM-D4**.

- [x] **MW5-1** Author the export `.def` source-of-truth
      (`windows_win32_shim.def`) — the single, ordered manifest of every
      `mwin32` `m<Name>` export. Names **not yet implemented** by this crate are
      commented out with a leading `;` so they are enabled by simply
      uncommenting as the entry points land; the currently-exported W forms and
      `NOT_SUPPORTED` stubs are active. Includes the MW8 find-Ex additions
      (`mFindFirstFileExW` active; `mFindFirstFileExA` commented, MW6). Mark
      `mCloseHandle` `; noalias` (exported but opt-out of auto-redirect).
- [x] **MW5-2** Alias generator (`alias_gen` module, no `unsafe`, platform-
      independent) mirroring `generate_mwin32_alias.cmake`: parse the `.def`,
      skip comment / `EXPORTS` / `noalias` lines, validate the `m([A-Z]|_)`
      shim shape, dedupe, and emit for each remaining `m<Name>` the
      `extern "C" void m<Name>();` decl, the decisive
      `extern "C" void (*__imp_<Name>)() = &m<Name>;` IAT slot, and the
      `#pragma comment(linker, "/alternatename:<Name>=m<Name>")` fallback, with
      a generated-file header. Unit-tested over synthetic `.def` snippets (noalias
      exclusion, comment skipping, dedupe, `m_lopen`→`_lopen` dusty-deck strip,
      invalid-shape error) and over the real `include_str!`'d `.def` (active /
      aliased counts, `mCloseHandle` excluded, a sample mapping). Record the
      realization in **SHIM-D4**.
- [x] **MW5-3** Define the NDJSON alias manifest + author the checked-in input
      (`windows_win32_shim_aliases.ndjson`). One JSON object per active alias —
      `{"win32":"RegOpenKeyExW","shim":"mRegOpenKeyExW"}` — with `shim` optional
      (defaults to `"m"` + `win32`) and an optional `"alias": false` for an
      exported-but-not-redirected name (e.g. `CloseHandle`). Extended format:
      blank lines and lines whose first non-blank chars are `#` or `//` are
      comment / section lines; not-yet-implemented entries are carried as
      comments so they enable by uncommenting. Populate with the current aliased
      roster (parity with the `.def` aliased set).
- [x] **MW5-4** `alias_obj` module (no `unsafe`, platform-independent): add the
      `object` crate (write API); parse the NDJSON via `tinyjson` line-by-line
      (skip blanks / comments, validate the `m<Name>` / `<Name>` shapes, dedupe),
      and emit an x64 COFF object — per alias an undefined external `m<Name>`, a
      defined public 8-byte `__imp_<Name>` in `.data` with an
      `IMAGE_REL_AMD64_ADDR64` relocation onto `m<Name>`, plus a single
      `.drectve` section carrying the `/alternatename:<Name>=m<Name>` directives.
      Round-trip unit tests: re-read the emitted COFF with `object` and assert
      the `__imp_` symbols, externals, relocations, and `.drectve` text; plus
      comment / blank handling, dedupe, malformed-record error, and a cross-check
      that the NDJSON aliased set equals `alias_gen`'s `.def` aliased set (drift
      guard).
- [x] **MW5-5** CLI tool `gen-alias-obj` (`src/bin/gen-alias-obj.rs`): read an
      NDJSON manifest path and write the COFF `.obj`. Document the link recipe
      (client links the emitted `.obj` + the cdylib import library). Record the
      COFF / NDJSON realization in **SHIM-D4**.
- [x] **MW5-6** C++ link-proof reusing the mwin32 C++ tree's link-proof program
      (`test/test_mwin32_alias.cpp`). `linkproof/linkproof_main.cpp` is that
      program with its GoogleTest harness replaced by a dependency-free `main`
      (the genuine-Win32 call sequence is unchanged). `linkproof/run-linkproof.ps1`
      drives the full production link recipe: `cargo build` the shim cdylib →
      `gen-alias-obj` emits the alias COFF → `cl /c` the link-proof TU → `link`
      the TU + alias `.obj` + `windows_win32_shim.dll.lib` → buffered `.pilcfg`
      + staged DLL → run. Verified: `dumpbin /imports` shows the EXE binds
      `mRegCreateKeyExW` / `mRegSetValueExW` / `mRegQueryValueExW` / `mRegCloseKey`
      from `windows_win32_shim.dll` (genuine `<windows.h>` `Reg*` calls redirected
      into the shim, not advapi32); and the runtime exit code flips with shim
      config — buffered `.pilcfg` ⇒ overlay captures the write, negative check
      finds the live key absent ⇒ exit 0; no `.pilcfg` (live passthrough) ⇒ the
      negative check finds the key ⇒ exit 1 — proving the redirect routes through
      the shim. The undecorated cdylib import library suffices (the Rust exports
      are `extern "system"` + `no_mangle`), so the separate undecorated import
      lib the C++ build needed is unnecessary here.
