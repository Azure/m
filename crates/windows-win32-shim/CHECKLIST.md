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

## MW6 — ANSI (A) variants (SHIM-D9, SHIM-D15)

Scope (SHIM-D15): MW6 adds the `A` entry points for the **functionally
implemented** `W` cores only (the observable-parity set). Each `A` form
transcodes its string arguments at the boundary (`CP_ACP`, MW6-1) and delegates
to the same safe `reg_ops` / `fs_ops` core the `W` export calls — *not* to the
`W` C-ABI export. `A` spellings of `NOT_SUPPORTED` `W` stubs are **not** added
(no observable behavior to mirror); they stay commented in the `.def` / `.ndjson`
until their `W` core lands.

- [x] **MW6-1** `ansi` boundary module (`src/ansi.rs`, no exports): `CP_ACP`
      transcoding via `windows-text` (`Utf16::{from_code_page,to_code_page}`).
      Helpers: `ansi_to_utf16` (NUL-terminated `LPCSTR` → `Utf16`), `fill_ansi_fixed`
      (`Utf16` → fixed `CHAR` buffer, truncate + NUL — the `WIN32_FIND_DATAA`
      `cFileName` / `cAlternateFileName` shape), `write_ansi_name` (`Utf16` →
      caller `LPSTR` with the `lpcch` char in/out contract — `RegEnumKeyExA` /
      `RegEnumValueA`), and registry value-DATA conversion `data_wide_to_ansi` /
      `data_ansi_to_wide` keyed on a string-type predicate (`REG_SZ`,
      `REG_EXPAND_SZ`, `REG_LINK`, `REG_MULTI_SZ` convert the whole buffer in one
      call — embedded / trailing NULs preserved; non-string types pass through).
      Owned behavior (Design Autonomy), unit-tested incl. a `REG_MULTI_SZ`
      embedded-NUL round-trip.
- [x] **MW6-2** Registry `A` forms in `mwinreg.rs` over the shared `reg_ops`
      core: `mRegOpenKeyExA`, `mRegCreateKeyExA`, `mRegDeleteKeyExA`,
      `mRegSetValueExA` (DATA `A`→`W`), `mRegQueryValueExA` (DATA `W`→`A`, sizes in
      ANSI bytes), `mRegDeleteValueA`, `mRegGetValueA` (DATA `W`→`A`),
      `mRegEnumKeyExA` (name `W`→`A`), `mRegEnumValueA` (name + DATA `W`→`A`),
      `mRegQueryInfoKeyA` (delegate; lengths reported in stored-form units — exact
      for ASCII, an owned simplification). Uncomment the 10 names in
      `windows_win32_shim.def` **and** `windows_win32_shim_aliases.ndjson`; bump
      the manifest count asserts (`alias_gen` 62/61, `alias_obj` 61) accordingly.
- [x] **MW6-3** Filesystem `A` forms in `mwinfile.rs` over the shared `fs_ops`
      core: `mCreateFileA`, `mDeleteFileA`, `mCreateDirectoryA`,
      `mRemoveDirectoryA`, `mSetFileAttributesA`, `mGetFileAttributesA`,
      `mGetFileAttributesExA` (identical out struct — delegate with the path
      transcoded), and the find family `mFindFirstFileA`, `mFindFirstFileExA`,
      `mFindFirstFileTransactedA`, `mFindNextFileA` (reuse the private
      `map_find_ex_params` + the MW8 `fs_ops` find core; a new `fill_find_data_ansi`
      mirrors `fill_find_data` with `CP_ACP` down-conversion of `cFileName` /
      `cAlternateFileName`). Uncomment the 11 names in the `.def` **and** `.ndjson`;
      bump the manifest count asserts again.

      > **⬅ CROSS-MILESTONE PREREQUISITE:** the `W` find-Ex core and search
      > predicate landed in MW8; this item reuses them.
- [x] **MW6-4** *(integration)* `tests/ansi_parity.rs`: `A`-writer / `W`-reader and
      `W`-writer / `A`-reader agree for registry values (`REG_SZ` + `REG_MULTI_SZ`,
      mirroring the C++ `test_mwinreg_value_ops` ANSI cases) over an in-memory
      `Registry` fixture, and an `A` directory enumeration's `cFileName`
      (reconstructed via the public `ansi` boundary) matches the `W` enumeration
      over an in-memory `Filesystem` tree. (Re-scoped from a live-FS scratch dir
      to the in-memory surface to match the established structural-isolation test
      pattern: deterministic, no OS dependency; the boundary transcoding is the
      sole behavior under test and the cores are surface-generic.)

## MW7 — End-to-end / C++ artifact parity — OUTLINE (detail when scheduled)

- [ ] **MW7-1** Load a C++-produced `.pilcfg` + `persisted_state` artifact and
      assert the Rust shim reproduces the C++ shim's observable behavior.
- [ ] **MW7-2** Packaging / SDK considerations (or record as out of scope).
- [ ] **MW7-3** *(integration)* Full end-to-end scenario: registry + filesystem
      through the shim under a single `.pilcfg`.

