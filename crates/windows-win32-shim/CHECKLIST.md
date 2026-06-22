# windows-win32-shim — CHECKLIST

Action-only checklist. Completed groups move to `COMPLETED-CHECKLIST.md`.
Decision references point at `DESIGN-NOTES.md` (`SHIM-D` numbers); the C++
`mwin32` DESIGN-NOTES (D1–D11) and its `test/` suite are the ABI behavioral spec.

Parallel all-Rust reimplementation of the C++ `mwin32` DLL, routing a Win32-shaped
C ABI through `windows-platform-isolation`. Scope: **filesystem and registry**.
Milestones are dependency-ordered, sized to ~5 items, and end in an integration
test. Sub-steps use decimal notation.

---

## MW1 — Foundation (scaffold, ABI posture, handle table, error mapping, session)

- [x] **MW1-1** Scaffold `crates/windows-win32-shim`: `Cargo.toml`
      (`crate-type = ["cdylib", "rlib"]`, edition 2024, MSRV inherited),
      `#![deny(unsafe_code)]` at the root with `#[allow(unsafe_code)]` only on the
      ABI-boundary modules (SHIM-D2), README + `DESIGN-NOTES.md` + `COMPONENT.md`.
      Dependencies (cfg(windows)): `windows-platform-isolation` (path),
      `windows-text` (path), `windows-sys` 0.59 (Win32 types/errors:
      `Win32_Foundation`, `Win32_System_Registry`, `Win32_Storage_FileSystem`).
      Add the crate to the workspace `members` list.
- [x] **MW1-2** Win32 error-mapping module (SHIM-D7): `registry_error_to_lstatus`
      and `filesystem_error_to_win32` translating `RegistryError` /
      `FilesystemError` into `LSTATUS` / Win32 codes, plus a `set_last_error`
      helper. `Os(u32)` passes through; structured variants map to documented
      codes. Owned mapping (Design Autonomy), with a unit table.
- [x] **MW1-3** Handle table (SHIM-D3 / mwin32 D11): mint `HANDLE`/`HKEY` with the
      reserved bit pattern; `intern` / `deref` / `close` behind a `Mutex` over a
      payload variant (isolation registry-key handle, file-handle state,
      find-enumeration state). Predefined `HKEY` values resolve (cached) to
      `windows-platform-isolation` well-known roots.
- [x] **MW1-4** Process-wide `Session`: lazily-initialized isolation stack holder
      vending the registry (and later filesystem) facade; default **live
      passthrough** for registry (SHIM-D8). Programmatic config only at this stage
      (`.pilcfg` is MW4). `current_exe()` is read via safe `std::env`.
- [x] **MW1-5** *(integration)* Tests: handle round-trip + reserved-bit
      invariants (minted handles never collide with predefined `HKEY`s or low-bit
      OS values), predefined `HKEY` → root resolution, and the error-mapping table.

## MW2 — Registry C ABI (W forms)

- [x] **MW2-1** `mRegOpenKeyExW` / `mRegCreateKeyExW` / `mRegCloseKey`
      (predefined handles are close no-ops), routing through the session's
      registry facade and minting result `HKEY`s.
- [x] **MW2-2** Value ops: `mRegSetValueExW`, `mRegQueryValueExW` (the Win32
      three-case size/type contract: query, `ERROR_MORE_DATA`, success),
      `mRegDeleteValueW`, `mRegGetValueW`. Value-type bytes map to/from
      `ValueData` (all six types).
- [x] **MW2-3** Enumeration / info: `mRegEnumKeyExW`, `mRegEnumValueW`,
      `mRegQueryInfoKeyW`, in `windows-platform-isolation` ordinal order.
- [x] **MW2-4** `mRegDeleteKeyExW` (subtree), plus `ERROR_NOT_SUPPORTED` stubs for
      the in-`.def` but unimplemented entries (transacted create, predefined-cache
      control, `mRegOverridePredefKey`, etc.), matching the C++ stub behavior.
- [x] **MW2-5** *(integration)* Registry tests mirroring the C++
      `test_mwinreg_predefined` / `test_mwinreg_open_close` /
      `test_mwinreg_value_ops` against a buffered in-memory stack, asserting the
      `LSTATUS` contracts and the `mRegQueryValueExW` three-case behavior.

## MW3 — Filesystem C ABI (W forms; metadata / dir / enum; content deferred)

> **⬅ CROSS-COMPONENT PREREQUISITE:** filesystem passthrough requires the live FS
> provider in `windows-platform-isolation` → **M9** (`LiveFilesystem`). See
> [`../windows-platform-isolation/CHECKLIST.md`](../windows-platform-isolation/CHECKLIST.md).
> Until M9 lands, MW3 is exercised against in-memory / artifact stacks only.

- [x] **MW3-1** `mCreateFileW` (creation disposition → create/open metadata node;
      mint a file `HANDLE` carrying handle state) + `mCloseHandle` (exported
      `noalias` / opt-in per SHIM-D4; predefined / unknown handles handled per the
      Win32 contract).
- [x] **MW3-2** Path metadata: `mGetFileAttributesW` / `mGetFileAttributesExW` /
      `mSetFileAttributesW` / `mDeleteFileW`, translating `FileMetadata` ↔ the
      Win32 attribute/`WIN32_FILE_ATTRIBUTE_DATA` shapes.
- [x] **MW3-3** Directory + handle-state ops: `mCreateDirectoryW` /
      `mRemoveDirectoryW`, and `mGetFileSizeEx` / `mSetFilePointerEx` over the
      file handle state.
- [x] **MW3-4** Directory enumeration: `mFindFirstFileW` / `mFindNextFileW` /
      `mFindClose` (find-enumeration state from `read_dir`, ordinal-ordered).
      Content + move/copy exports (`mReadFile`, `mWriteFile`,
      `mReadFileScatter`/`mWriteFileGather`, `mMoveFileExW`, `mCopyFileExW`)
      return the Win32 not-supported failure shape (SHIM-D6; `mMoveFileExW`
      additionally awaits a future isolation rename op).
- [x] **MW3-5** *(integration)* Filesystem tests mirroring the C++
      `test_mwinfile_handle_meta` / `test_mwinfile_legacy` against the live FS
      provider over a scratch temp dir (RAII cleanup) and an in-memory artifact
      stack; assert attribute/size results and ordinal `FindFirst`/`FindNext`
      ordering.

## MW4 — `.pilcfg` config (JSON sidecar; artifact parity, SHIM-D5)

- [x] **MW4-1** Choose the JSON parser dependency; model the `.pilcfg` schema
      (`buffer_updates`, `record_modifications`, `redirections`,
      `persisted_state`, `capture_snapshot`, `diagnostic_log`, `fault_script`;
      `webcore` ignored), with `%TEMP%`-style expansion as the C++ does.
- [x] **MW4-2** Tolerant sidecar load: resolve `<current_exe>.pilcfg`
      (`std::env::current_exe`, safe); absent / unreadable / malformed →
      passthrough, never failing the host (mwin32 D5).
- [x] **MW4-3** Wire config → isolation stack composition: `buffer_updates` →
      `Buffered` layer; `persisted_state` → load the `<Platform>` artifact via the
      isolation loaders; `redirections` / `record_modifications` / `fault_script`
      honored where the isolation crate supports them, documented as gaps
      otherwise.
- [x] **MW4-4** *(integration)* `.pilcfg` parity tests (mirror C++ `test_pilcfg`
      + buffered `test_mwinreg_value_ops`): a buffered fixture isolates writes
      from the live registry; `capture_snapshot` writes state on teardown.

## MW5 — Link-time Win32→`m` alias (redirection, SHIM-D4)

- [ ] **MW5-1** Author the export `.def` (parity with `mwin32.def` for the
      in-scope registry + filesystem names, **including the `FindFirstFileEx`
      / `FindFirstFileTransacted` family added by MW8** — `mFindFirstFileExW`,
      `mFindFirstFileExA`, `mFindFirstFileTransactedW`, `mFindFirstFileTransactedA`);
      mark `mCloseHandle` `noalias`.

      > **⬅ CROSS-MILESTONE PREREQUISITE:** the Ex/Transacted `W` exports are
      > minted in MW8; their `A` forms in MW6. Add their alias/`/alternatename`
      > slots here once those entry points exist.
- [ ] **MW5-2** Alias generation: emit the `__imp_<Name> = m<Name>` IAT-slot
      definitions + `/alternatename` directives from the export list (build
      script or a generator mirroring the C++ `generate_mwin32_alias.cmake`).
- [ ] **MW5-3** Produce the alias object / import lib and wire the link recipe so
      a consumer opts in by linking it alongside the shim.
- [ ] **MW5-4** *(integration)* Link-proof test (port of `test_mwin32_alias.cpp`):
      genuine `<windows.h>` `RegOpenKeyExW` / `CreateFileW` calls (no shim
      headers) are redirected through the shim and observe the isolated state.

## MW6 — ANSI (A) variants — OUTLINE (detail when scheduled, SHIM-D9)

- [ ] **MW6-1** `A`↔`W` boundary transcoding via `windows-text` (`CP_ACP`).
- [ ] **MW6-2** Registry `A` forms delegating to the `W` implementations.
- [ ] **MW6-3** Filesystem `A` forms delegating to the `W` implementations,
      **including `mFindFirstFileExA` and `mFindFirstFileTransactedA`** (transcode
      `lpFileName` via MW6-1, then call the shared `W` find-Ex core from MW8;
      `WIN32_FIND_DATAA` out-fill mirrors `fill_find_data` with `CP_ACP`
      down-conversion of `cFileName` / `cAlternateFileName`).

      > **⬅ CROSS-MILESTONE PREREQUISITE:** the `W` find-Ex core and search
      > predicate land in MW8; this item reuses them.
- [ ] **MW6-4** *(integration)* `A`/`W` parity tests.

## MW7 — End-to-end / C++ artifact parity — OUTLINE (detail when scheduled)

- [ ] **MW7-1** Load a C++-produced `.pilcfg` + `persisted_state` artifact and
      assert the Rust shim reproduces the C++ shim's observable behavior.
- [ ] **MW7-2** Packaging / SDK considerations (or record as out of scope).
- [ ] **MW7-3** *(integration)* Full end-to-end scenario: registry + filesystem
      through the shim under a single `.pilcfg`.

## MW8 — `FindFirstFileEx` family completeness (wildcard matching, search ops, SHIM-D14)

Closes the long-deferred SHIM-D12 gap (the find core currently captures **all**
children and ignores the pattern leaf) and adds the extended `Ex` / `Transacted`
enumeration entry points. Without this, a client's genuine `FindFirstFileExW`
falls through to the real OS API and escapes the sandbox, and `mFindFirstFileW`
over-matches (returns every child regardless of pattern). Behavior is owned by
us (Design Autonomy): we **specify** Win32 `FsRtlIsNameInExpression`-style DOS
wildcard semantics; the matcher itself is an ordinal string primitive that lives
in `windows-text` (next to `OrdinalCasing`, see `WT-6`) and is consumed here.
This milestone also depends on the 8.3 short-name plumbing from
`windows-platform-isolation` → M10, so `fill_find_data` can emit
`cAlternateFileName` for `FindExInfoStandard`. The `A` forms are queued in
MW6-3 and the alias/`.def` exports in MW5-1.

> **⬅ CROSS-COMPONENT PREREQUISITE:** the wildcard matcher is `windows-text`
> → `WTM-1` (`WT-6`). See [`../windows-text/CHECKLIST.md`](../windows-text/CHECKLIST.md).
>
> **⬅ CROSS-COMPONENT PREREQUISITE:** the 8.3 short name on `DirEntry` is
> `windows-platform-isolation` → `M10`. See
> [`../windows-platform-isolation/CHECKLIST.md`](../windows-platform-isolation/CHECKLIST.md).

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
- [ ] **MW8-2** Add the `mFindFirstFileExW` entry point in `mwinfile.rs`:
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
- [ ] **MW8-3** *(integration)* End-to-end `W` find-Ex tests over a
      `TreeFsSurface<Win32OrdinalCasing>` fixture: wildcard `*.txt` (subset),
      `?` single-char, literal exact, `LimitToDirectories` (files excluded),
      case-sensitive vs default casing, `FindExInfoBasic` parity with
      `Standard`, large-fetch flag ignored, invalid info-level / search-op /
      flag-bit / non-null filter ⇒ `ERROR_INVALID_PARAMETER`, no-match ⇒
      `ERROR_FILE_NOT_FOUND` + `INVALID_HANDLE_VALUE`, and an isolation
      assertion that no live-OS path is touched. Verify `mFindNextFileW`
      continues to honor the stored predicate across the enumeration.
