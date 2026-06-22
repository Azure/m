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
