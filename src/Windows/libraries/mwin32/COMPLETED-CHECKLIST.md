# mwin32 completed checklist

## Moved 2025-05-27 — mwin32 registry shim M1–M4 (session bootstrap, `.pilcfg`, value ops, redirections + persisted state)

### Milestone M1 — Session bootstrap + predefined-key resolution

- [x] M1-1: Add `m::pil::make_platform_interface(flags, redirections)` to the pil
      public API (`m/pil/pil.h`) returning `std::shared_ptr<iplatform>`, and refactor
      `make_platform` to share its flag-mapping. Gives interface-level access to the
      PIL stack (the value-wrapper `platform`/`registry_class` cannot yield raw `ikey`).
- [x] M1-2: Add mwin32 `session` (`src/session.h` / `src/session.cpp`): process-wide,
      lazily-initialized PIL platform + registry (default passthrough), thread-safe;
      a per-predefined-key `ikey` cache populated via `iregistry::open_predefined_key`;
      free helpers `is_predefined_handle_value(uintptr_t)` and
      `try_resolve_predefined_ikey(uintptr_t)`.
- [x] M1-3: Route resolution through the handle layer chokepoint:
      `handle_table::deref_handle<shared_ptr<ikey>>` resolves predefined HKEY values via
      the session before the table lookup; `handle_table::close` is a no-op on predefined
      pseudo-handles. (No `mReg*` call sites change.)
- [x] M1-4: Add `session.cpp` to `src/CMakeLists.txt`; clean debug build.
- [x] M1-5: Read-only integration test through the C ABI: open `HKEY_CURRENT_USER\Software`
      (passthrough), close it, and confirm closing a predefined handle is a success no-op.
- [x] M1-6: Clean debug + release build; debug + release tests pass.

### Milestone M2 — `.pilcfg` sidecar configuration

- [x] M2-1: Add a JSON dependency (nlohmann-json) to `vcpkg.json`.
- [x] M2-2: Locate `<executable>.pilcfg` next to the host module (GetModuleFileNameW).
- [x] M2-3: Define + parse the JSON schema selecting the PIL stack modes the
      `make_platform_interface` factory supports today — passthrough (default),
      buffered (`buffer_updates`), and logging (`record_modifications`) — and build the
      session's platform from it. Absent/invalid file falls back to passthrough.
      (Re-scoped: redirections and persisted-state were dropped from M2; the factory
      cannot accept runtime-built redirections — its param is an `initializer_list` —
      and has no persisted-state load capability. Queued as M4 below.)
- [x] M2-4: Tests for config parsing (passthrough default, buffered, logging, invalid
      JSON, non-object root, non-boolean members, unknown members).

### Milestone M3 — Registry value operations

- [x] M3-1: Implement `mRegSetValueExW` → `ikey::set_value`.
- [x] M3-2: Implement `mRegQueryValueExW` → `ikey::get_value` (size query, ERROR_MORE_DATA, type-out).
- [x] M3-3: Implement the `*A` variants with ANSI↔UTF-16 conversion for string value types.
- [x] M3-4: C-ABI integration tests (buffered mode) for round-trips: REG_DWORD, REG_SZ,
      REG_BINARY, REG_MULTI_SZ; size query; ERROR_MORE_DATA; type-out.

### Milestone M4 — `.pilcfg` redirections + persisted state (follow-on)

- [x] M4-1: Change the PIL redirections parameter (`make_platform` /
      `make_platform_interface` / `create_platform_interface` / `redirecting::platform`
      / `redirector`) from `std::initializer_list<...>*` to a runtime-constructible
      `std::span<std::pair<std::u16string_view, std::u16string_view> const>` so config
      data can drive it. All current callers pass `nullptr`/default.
- [x] M4-2: Extend the `.pilcfg` schema + parser with a `redirections` array
      (`{ "from": "...", "to": "..." }`) and feed it through the session.
- [x] M4-3.1: Complete the buffered layer's XML serialization. `key::save_xml` is
      currently a stub; implement it to recursively emit the overlay's materialized
      subkeys and set values (value `type` + hex-encoded `data`, plus `deleted`
      tombstones for keys and values) under the existing `<Registry>/<Key>` schema.
      Add a local hex-encode helper. Unit test that a buffered overlay saves to the
      expected XML.
- [x] M4-3.2: Implement load. Add a `buffered::platform` snapshot factory that
      builds a platform from a persisted XML file over a null underlying registry,
      reconstructing predefined keys, subkeys, and values as fully-materialized
      (non-mirrored) nodes. Round-trip test: save an overlay, load it, read the
      values back.
- [x] M4-3.3: Expose load through the PIL public API and wire it into mwin32:
      extend the `.pilcfg` schema/parser with a `persisted_state` path string and
      have the session load the snapshot platform when it is set (mode (c): run
      against a persisted snapshot without touching the live registry). Tests for
      parsing and for the session selecting the snapshot.


## Moved 2026-06-16 — Milestone M-FS-SHIM (Win32 filesystem API shim: `mCreateFileW`, `mFindFirstFileW`, …)

Goal: expose the PIL **filesystem surface** (`ifilesystem`, complete in `src/libraries/pil`)
through Win32-shaped entry points, mirroring the `mReg*` registry family. The shims model the
**Win32 filesystem APIs** (`CreateFileW`, `FindFirstFileW`, `GetFileAttributesExW`, …) — **not**
the C++ `std::filesystem` API — so an unmodified `<windows.h>` client redirects through
`mwin32_alias` with no source change. Each shim redirects through the process-wide `session` into
`iplatform::get_filesystem()`; the mode (passthrough / buffered / redirecting / logging / fault)
is selected by the `.pilcfg` sidecar.

- [x] M-FS-SHIM-1: Extend `handle_table` (`data_variant_type`) to additionally hold
      `std::shared_ptr<m::pil::ifile>` and a find-enumeration state object (the cursor + buffered
      results of an `enumerate_entries` call). Update `intern` overloads and `resolve` to dispatch
      by `std::variant` alternative; predefined-handle resolution stays registry-only.
- [x] M-FS-SHIM-2: Add `mwinfile.{h,cpp}` with the **non-handle metadata / namespace** ops first
      (no `handle_table` needed): `mCreateDirectoryW/A`, `mRemoveDirectoryW/A`, `mDeleteFileW/A`,
      `mMoveFileW/A` + `mMoveFileExW/A`, `mGetFileAttributesW/A` + `mGetFileAttributesExW/A` →
      `get_filesystem()` → PIL `create_directory` / `remove_entry` / `rename_entry` /
      `query_information`. `*A` variants convert via `m::acp_to_basic_string<char16_t>` (CP_ACP),
      then build `file_path` (same pattern as `mwinreg.cpp`'s `to_key_path`).
- [x] M-FS-SHIM-3: Per-entry-point PIL exception / `std::error_code` → **Win32 last-error** mapping
      (filesystem APIs report failure via `SetLastError` + a `BOOL` / `INVALID_HANDLE_VALUE`
      return, *not* `LSTATUS`). Sibling to the registry exception→`LSTATUS` mapper, sharing only the
      cross-cutting exception/`error_code` translation; OOM / exceptions never cross the C ABI.
      **No shared `disposition` mapping** — each verb's `disposition` is interpreted at its own call
      site, because flags/result-codes are owned by the specific virtual function, not the
      interface (DESIGN-NOTES D12).
- [x] M-FS-SHIM-4: `mCreateFileW/A`: map `dwDesiredAccess` / `dwCreationDisposition` /
      `dwShareMode` onto PIL `open_file` vs `create_file`; intern the returned `ifile` in
      `handle_table` and return the minted `HANDLE`. **Content is out of scope** (D14): a
      `CreateFile` that opens an existing entry resolves metadata only. `ReadFile` / `WriteFile`
      and every other handle-consuming content API are **not** aliased in this milestone, so the
      minted handle's only valid consumers here are the handle-based metadata calls and
      `mCloseHandle` — a client that passes it to an un-aliased content API gets the real API and
      `ERROR_INVALID_HANDLE` (the D11 handle-translation invariant). Content through a minted
      handle lights up in **M-FS-CONTENT**; document this boundary in the shim.
- [x] M-FS-SHIM-5: Find family: `mFindFirstFileW/A` → `enumerate_entries`, store the enumeration
      state in `handle_table`, fill `WIN32_FIND_DATAW/A` for the first entry; `mFindNextFileW/A`
      advances the cursor (`ERROR_NO_MORE_FILES` at the end); `mFindClose` releases the state.
- [x] M-FS-SHIM-6: `mCloseHandle` routing — because files use the generic `CloseHandle`, the shim
      inspects the handle: a value minted by `handle_table` (recognizable by its reserved bit
      pattern, see `handle_table.h`) is released from the table; any other value forwards to the
      real `::CloseHandle`. Document that this shim is broader than `mRegCloseKey` (it must not
      break non-file handles).
- [x] M-FS-SHIM-7: Add the new names to [mwin32.def](mwin32.def) so the generated `mwin32_alias`
      IAT-redirect object redirects a client's genuine `CreateFileW` / `FindFirstFileW` /
      `GetFileAttributesW` / … with no source change (subject to the documented D8 limits —
      `GetProcAddress`-resolved calls still need the runtime-interception envelope). Note that
      `CloseHandle` aliasing is opt-in and carries the broader-than-registry caveat from
      M-FS-SHIM-6.
- [x] M-FS-SHIM-8 (integration): Sample client (or test) links `mwin32_alias`, supplies a
      `.pilcfg` selecting a **redirecting** (and a **buffered**) filesystem, and uses genuine
      `CreateFileW` / `CreateDirectoryW` / `FindFirstFileW` / `GetFileAttributesExW` / `MoveFileExW`
      through the shim ABI; assert paths are redirected / captured and the metadata round-trips,
      and that `CloseHandle` on a non-file handle still reaches the real API.

## Moved 2026-06-16 — Milestone M-FS-HANDLE-META (handle-based filesystem metadata APIs)

Goal: alias the handle-consuming **metadata** APIs the D11 handle-translation invariant requires,
served entirely from the existing `ifile::query_information` (no PIL change). Each resolves the
minted pseudo-handle via `handle_table` and serves/mutates metadata; none touch byte content
(D14). Metadata is read-only on the PIL surface this milestone, so the Set* verbs are an accepted
no-op and content/allocation classes report the deferred-content error (DESIGN-NOTES D13).

- [x] M-FS-HANDLE-META-1: `mGetFileInformationByHandle`, `mGetFileSize` / `mGetFileSizeEx` —
      resolve the `ifile`, fill `BY_HANDLE_FILE_INFORMATION` / size from `query_information`.
- [x] M-FS-HANDLE-META-2: `mGetFileInformationByHandleEx` / `mSetFileInformationByHandle` — handle
      the *metadata* `FILE_INFO_BY_HANDLE_CLASS` classes (basic, standard, name, rename,
      disposition); allocation / EOF / content classes return the deferred-content error
      (M-FS-CONTENT).
- [x] M-FS-HANDLE-META-3: `mGetFileTime` / `mSetFileTime`, `mGetFileType` (interned handle →
      `FILE_TYPE_DISK`), `mGetFinalPathNameByHandleW/A` (handle → path; redirecting maps
      private→public).
- [x] M-FS-HANDLE-META-4: `.def` additions + a test that opens via `mCreateFileW`, reads metadata
      by handle, and round-trips timestamps / attributes under buffered + redirecting.


## Moved 2026-06-16 — Milestone M-FS-COPY (copy / replace / extended namespace & path APIs)

Covered the remaining path-based namespace/metadata APIs the inventory (D11) marks S / S/ns that
M-FS-SHIM did not include. No handle content involved. See DESIGN-NOTES.md D14.

- [x] M-FS-COPY-1: `mCopyFileW/A`, `mCopyFileExW/A`, `mCopyFile2` — namespace copy (create dest node
      from source metadata); progress / cancel callbacks ignored under isolation. Whole-file byte
      copy depends on content (M-FS-CONTENT); document the boundary.
- [x] M-FS-COPY-2: `mReplaceFileW/A` — namespace re-key + backup node.
- [x] M-FS-COPY-3: `mCreateDirectoryExW/A`, `mGetTempFileNameW/A` (mint + create in a redirectable
      directory), `mSetFileAttributesW/A`.
- [x] M-FS-COPY-4: path resolution — `mGetFullPathNameW/A` (route through PIL `file_path`, PIL D11),
      `mGetLongPathNameW/A`, `mSearchPathW/A`.
- [x] M-FS-COPY-5: `.def` additions + integration test (copy + replace + temp-file + path
      canonicalization through the redirecting filesystem).


## Moved 2026-06-16 — Milestone M-FS-NOTIFY (change-notification shim onto the PIL monitor)

Surfaced the Win32 change-notification family onto the already-complete PIL filesystem monitor
(`ifilesystem::monitor()`, PIL D15) — no PIL change required. Live-provider-only (the buffered
overlay does not model live change); the detailed and coarse paths share the monitor. See
DESIGN-NOTES.md D15. A redirected-watch path-shape reconciliation is queued as PIL
M-FS-MONITOR-REDIR.

- [x] M-FS-NOTIFY-1: `mReadDirectoryChangesW` / `mReadDirectoryChangesExW` → `register_watch` on
      `ifilesystem::monitor()`; map `FILE_NOTIFY_CHANGE_*` ↔ `register_watch_flags`; decode the
      monitor's detailed change records into the `FILE_NOTIFY_INFORMATION` chain.
- [x] M-FS-NOTIFY-2: `mFindFirstChangeNotificationW/A`, `mFindNextChangeNotification`,
      `mFindCloseChangeNotification` — coarse event-only wrappers over the same monitor; the
      notification handle is a real OS-waitable event held in a side registry (it cannot be a minted
      pseudo-handle because the shim does not intercept `WaitForSingleObject`).
- [x] M-FS-NOTIFY-3: `.def` additions + a test asserting a directory mutation under a passthrough
      (live) provider surfaces through `mReadDirectoryChangesW` with the right action + name.


## Moved 2026-06-16 — Milestone M-FS-CONTENT (handle-translation for byte content, D11 + D16)

Completed the D11 handle-translation invariant for content-bearing consumers using the
redirection-backed whole-file content model (D16): passthrough / redirecting serve real bytes,
buffered byte-mutation and partial / mid-file mutation return the documented deferred-content
error (ERROR_NOT_SUPPORTED). 17 new exports; alias count 160 → 177.

- [x] M-FS-CONTENT-1: `mReadFile` / `mReadFileEx` / `mReadFileScatter`, `mWriteFile` / `mWriteFileEx`
      / `mWriteFileGather` — translate pseudo→`ifile`, forward to the backing source (passthrough /
      redirecting); buffered byte-mutation returns the documented deferred-content error (D16
      non-goal).
- [x] M-FS-CONTENT-2: positioning + size on the handle — `mSetFilePointer` / `mSetFilePointerEx`,
      `mSetEndOfFile` / `mSetFileValidData`; whole-file replacement allowed, partial byte / size
      mutation rejected per D16.
- [x] M-FS-CONTENT-3: `mFlushFileBuffers`, `mLockFile` / `mLockFileEx`, `mUnlockFile` /
      `mUnlockFileEx`, `mDeviceIoControl`, `mDuplicateHandle` — translate the handle, forward;
      `mDuplicateHandle` mints a second `handle_table` entry for the same `ifile`.
- [x] M-FS-CONTENT-4: `.def` additions + integration test: `CreateFile`→`WriteFile` (whole-file)
      →`CloseHandle`→`CreateFile`→`ReadFile` round-trips through a redirecting filesystem; a partial
      byte-range overwrite returns the documented unsupported error.


## Moved 2026-06-16 — Milestone M-FS-LEGACY (dusty-deck legacy file APIs, D11 coverage)

Covered the legacy ("dusty deck") file primitives the D11 inventory reclassified from out-of-scope
to covered, reusing the same `handle_table` and handle-translation layers. Namespace / metadata
legacy parts (open / create / transacted) needed only M-FS-SHIM; content legacy parts (`_l*` / `_h*`
/ LZ) needed M-FS-CONTENT. The LZ compress / expand family is a passthrough (no decompression
modeled, D16). 15 new exports for the content family; alias count 177 → 192.

- [x] M-FS-LEGACY-1: `mOpenFile` (`OFSTRUCT`), `m_lopen` / `m_lcreat` — redirect the path, mint an
      `HFILE` from `handle_table`; map `OF_*` onto creation disposition. (Needs M-FS-SHIM only.)
- [x] M-FS-LEGACY-2: Transacted variants — `mCreateFileTransactedW/A`, `mMove/CopyFileTransacted*`,
      `m*DirectoryTransacted*`, `m*FileAttributesTransacted*`, `mFindFirst*Transacted*`,
      `mGetLongPathNameTransacted*` — alias onto the non-transacted PIL op, **ignore** the
      transaction handle (D11). (Needs M-FS-SHIM + M-FS-COPY for the copy / move forms.)
- [x] M-FS-LEGACY-3: legacy content — `m_lread` / `m_lwrite` / `m_hread` / `m_hwrite` / `m_llseek` /
      `m_lclose`, `mLZOpenFile` / `mLZRead` / `mLZSeek` / `mLZClose` / `mLZCopy` / `mLZInit` /
      `mGetExpandedName` — translate the minted `HFILE` / LZ handle, forward (passthrough). (Needs
      M-FS-CONTENT.)
- [x] M-FS-LEGACY-4: `.def` additions + a test driving a dusty-deck `OpenFile`→`_lread`→`_lclose`
      sequence through the redirecting filesystem.

## Moved 2026-06-19 — Wire capture (real-socket HTTP contract lifecycle demo) complete (WC-1…WC-11)

Winsock interception in the link-time alias (tee, never alters bytes — D6) +
HTTP/1.1 `Content-Length` reassembler + `.pilcfg` capture schema (record/validate)
wired to the PIL contract recorder/validator + raw-Winsock client/server sample
apps with both-direction fault injection + an end-to-end derive→detect integration
suite proven transport-independent across IPv4 / IPv6 / DNS / synthetic. v1 scope:
HTTP/1.1 Content-Length framing only. See DESIGN-NOTES D19–D30.

### Milestone M-WIRECAP-SOCK — Winsock interception

- [x] **WC-1**: Winsock shims (`msocket`, `mconnect`, `maccept`, `msend`, `mrecv`,
  `mclosesocket`, `mWSASend`/`mWSARecv`) forwarding to genuine `ws2_32` and teeing
  bytes per socket; `.def` + alias exports; byte-identical passthrough smoke test.
- [x] **WC-2**: HTTP/1.1 reassembler (request/response streams, `Content-Length`
  framed, partial reads, keep-alive pipelining); unit tests with canned streams.
- [x] **WC-3**: Capture sink seam — pure side-channel (D6) receiving reassembled
  crossings; unit tests assert byte forwarding is unaffected.

### Milestone M-WIRECAP-CFG — pilcfg wiring + capture modes

- [x] **WC-4**: `.pilcfg` `capture` schema (`mode = record | validate`, spec path,
  optional filter); parser + unit tests.
- [x] **WC-5**: Wire the sink to PIL — `record` feeds `make_http_contract_recorder`
  and emits YAML at shutdown; `validate` loads the spec and tallies
  `validate_request`/`validate_response` violations per direction; unit tests.

### Milestone M-WIRECAP-SAMPLES — real-socket sample apps

- [x] **WC-6**: Raw-Winsock HTTP/1.1 **server** sample linking `mwin32_alias`;
  `--family ipv4|ipv6|dual`, ephemeral port echo, response-direction fault switch.
- [x] **WC-7**: Raw-Winsock HTTP/1.1 **client** sample linking `mwin32_alias`;
  `--target dns/ipv4/ipv6`, request-direction fault switch.
- [x] **WC-8**: CMake wiring for both samples (alias-linked, installed under the
  SDK examples) + hand-authored reference OpenAPI YAML cross-checking the derived
  spec.

### Milestone M-WIRECAP-INTEG — end-to-end lifecycle test (topology matrix)

- [x] **WC-9**: Reusable in-process harness (server + client on two threads over a
  real loopback socket; ephemeral port read-back; IPv4/IPv6/DNS/synthetic
  selector) + derive phase asserting clean traffic yields a loadable OpenAPI spec.
- [x] **WC-10**: Detect phase over IPv4 — load the derived YAML in validate mode,
  inject faults in both directions, assert a request and response violation while
  both connections complete.
- [x] **WC-11**: Transport matrix — re-run derive→detect over IPv6, DNS, and
  synthetic; assert the derived spec and violation tallies are equivalent across
  all transports (the transport-independence result).
