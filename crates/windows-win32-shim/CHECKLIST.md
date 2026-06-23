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

Re-planned on execution (twice): this crate is a Rust `cdylib`, not the C++
build. The `cargo`-verifiable core is the alias manifest plus a generator with
unit tests. MW5-1/MW5-2 first realized this as a `.def` + a C++-text generator
(`alias_gen`). MW5-3..MW5-5 replace the `cl.exe`-compiled-text path with a
**pure-Rust COFF emitter** driven by a checked-in NDJSON manifest: we write the
alias object's bytes directly (via the `object` crate), so producing the alias
artifact needs **no C++ compiler and no MSVC tool at all** — only the client's
own linker consumes it. NDJSON is chosen so the versioned input supports
comment / section lines (the "extended" format). The C++ link-proof EXE remains
a genuinely cross-toolchain verification (MW5-6) and stays deferred.

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
- [ ] **MW5-4** `alias_obj` module (no `unsafe`, platform-independent): add the
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
- [ ] **MW5-5** CLI tool `gen-alias-obj` (`src/bin/gen-alias-obj.rs`): read an
      NDJSON manifest path and write the COFF `.obj`. Document the link recipe
      (client links the emitted `.obj` + the cdylib import library). Record the
      COFF / NDJSON realization in **SHIM-D4**.
- [ ] **MW5-6** *(deferred — cross-toolchain, not a `cargo test`)* C++ link-proof
      EXE consuming the emitted alias `.obj`: genuine `<windows.h>`
      `RegOpenKeyExW` / `CreateFileW` calls (no shim headers) redirected through
      the shim observe the isolated state. Requires a C++ consumer in the build;
      tracked until a harness approach is chosen.

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

