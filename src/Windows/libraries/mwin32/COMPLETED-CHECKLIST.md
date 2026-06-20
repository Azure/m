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

## Moved 2026-06-19 — HWC `mWebCore*` shim + `.pilcfg` OpenAPI/Swagger contract binding (PIL D-HWC-1…D-HWC-11)

The mwin32 side of the Hostable Web Core surface: the `mWebCore*` Win32 shim entry points
(M-HWC-SHIM) and the `.pilcfg` `webcore.contracts` binding that wires PIL's OpenAPI/Swagger
contract surface onto the HWC HTTP edge in validate/drive mode (M-HWC-CONTRACTCFG), including the
production live-edge wiring (CONTRACTCFG-7) unblocked by PIL M-HWC-ENGINE-EDGE.

## Milestone M-HWC-SHIM — `mWebCore*` Hostable Web Core shim

Goal: expose the PIL **HWC engine surface** (`iwebcore`, designed in
[`src/libraries/pil/DESIGN-NOTES.md`](../../../libraries/pil/DESIGN-NOTES.md) decisions
**D-HWC-1 … D-HWC-7**) through Win32-shaped `mWebCore*` entry points, mirroring the `mReg*`
shims. Each shim redirects through the process-wide session into the active PIL HWC surface; the
mode (passthrough / logging / fault / interception) is selected by the `.pilcfg` sidecar.

- [x] M-HWC-SHIM-1: Add `mwinhwc.{h,cpp}` exporting `mWebCoreActivate(PCWSTR pszAppHostConfigFile,
      PCWSTR pszRootWebConfigFile, PCWSTR pszInstanceName)`, `mWebCoreShutdown(DWORD fImmediate)`,
      and `mWebCoreSetMetadata(PCWSTR pszMetadataType, PCWSTR pszValue)`, all returning `HRESULT`
      (verified against the SDK `um/hwebcore.h`). Each gets the process-wide `session`'s
      `iplatform`, calls `get_webcore()`, converts the `PCWSTR` config paths to `file_path`
      values in the isolated filesystem (UTF-16 already — no CP_ACP dance), and forwards.
- [x] M-HWC-SHIM-2: Enforce single-activation-per-process on the session (it holds the one
      `iwebcore_instance`); a second `mWebCoreActivate` returns
      `HRESULT_FROM_WIN32(ERROR_SERVICE_ALREADY_RUNNING)`, and `mWebCoreShutdown` with no active
      instance returns `HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE)` — matching the real engine.
      No `handle_table` involvement (HWC has no handle in its ABI).
- [x] M-HWC-SHIM-3: Centralized PIL `disposition` / `std::error_code` → `HRESULT` mapping at the
      C ABI boundary (sibling to the existing exception→`LSTATUS` mapping), so OOM / exceptions
      never cross the ABI.
- [x] M-HWC-SHIM-4: Extend `.pilcfg` parsing with an optional `webcore` object
      (`interception` mode, `endpoints` table, optional `materialization_dir` / `fault_script`);
      `build_platform_from_config` wraps the webcore surface like the fault layer.
- [x] M-HWC-SHIM-5: Add the three `mWebCore*` names to [mwin32.def](mwin32.def); the generated
      `mwin32_alias` IAT-redirect object then redirects a client's genuine `WebCoreActivate` /
      `WebCoreShutdown` / `WebCoreSetMetadata` with no source change (subject to the documented
      D8 limits — `GetProcAddress`-resolved calls still need the runtime-interception envelope).
- [x] M-HWC-SHIM-6 (integration): Sample client (or test) links `mwin32_alias`, supplies a
      `.pilcfg` selecting a passthrough/logging webcore with a fake engine, and drives
      activate / already-activated / shutdown / set_metadata through the shim ABI; assert the
      `HRESULT` shapes match the real engine contract.
      Note: Implemented as test_mwinhwc.cpp with tests against the null webcore provider.

## Milestone M-HWC-CONTRACTCFG — `.pilcfg` OpenAPI/Swagger contract binding (PIL D-HWC-8)

Goal: let a `.pilcfg` reference the team's OpenAPI (Swagger) specs and bind each to a
webcore endpoint in `validate` and/or `drive` mode, wiring the PIL contract surface onto
the HWC HTTP edge. The contract surface itself (loader, `ihttp_contract`, validating facet,
example driver) is PIL Phase 4.

- [x] M-HWC-CONTRACTCFG-1: Extend `pilcfg::webcore_config`
      ([`src/pilcfg.h`](src/pilcfg.h)) with a `contracts` vector: each entry carries a `spec`
      host path (`%VAR%`-expanded, D17), an `endpoint` logical key (taken literally, like
      `webcore.endpoints`), and a `mode` enum (`validate` / `drive`). Default: empty (no
      contracts).
- [x] M-HWC-CONTRACTCFG-2: Parse the optional `webcore.contracts` array in
      [`src/pilcfg.cpp`](src/pilcfg.cpp) next to `read_endpoints_member` — strict like the rest
      of `parse_pilcfg` (non-array throws; each element must be an object with string `spec`,
      string `endpoint`, and `mode` one of `"validate"`/`"drive"`; wrong type/shape throws).
      Apply `%VAR%` expansion to `spec` only.
- [x] M-HWC-CONTRACTCFG-3: In `webcore_config_platform` (built by `build_platform_from_config`),
      load and bind every `webcore.contracts` entry: read the spec bytes from the
      (`%VAR%`-expanded) host path, call PIL `get_http_contract().load(...)` to produce a
      validating document, and hold the bound documents keyed by endpoint + mode
      (`load_webcore_contracts`). A missing/malformed spec is tolerant (best-effort, per D5/D7):
      it leaves that binding absent rather than breaking the host.

      Note (re-plan, execution finding): the live-edge *attachment* the original wording implied —
      auto-validating real request/response traffic and executing the example driver against the
      running engine — is gated on the webcore interception edge, which `webcore_config_platform`
      still forwards as a placeholder (no public attach hook exists yet). That work is split out as
      CONTRACTCFG-6 below. The bound documents are reachable via the public PIL surface
      (`validate_request` / `validate_response` / `synthesize_requests` / `drive_contract`), which
      is what CONTRACTCFG-5 exercises with a fake engine.
- [x] M-HWC-CONTRACTCFG-4 (unit tests): `parse_pilcfg` tests for `webcore.contracts` —
      absent (empty), single entry, multiple entries (order preserved), `%VAR%` expansion of
      `spec`, and the negative cases (non-array, element not an object, missing/empty `spec` or
      `endpoint`, unknown `mode`). ≥10 cases, sub-second.
- [x] M-HWC-CONTRACTCFG-5 (integration): a `.pilcfg` referencing a small YAML spec binds through
      `load_webcore_contracts`, then drives and validates a fake engine end to end via the public
      drive surface (`drive_contract(document, submit)`) — assert the configured mode produced the
      expected request traffic and that a deliberately non-conforming response is reported as a
      contract violation.
- [x] M-HWC-CONTRACTCFG-6: attach the bound contracts to a PIL contract edge
      (`m::pil::ihttp_contract_edge`, M-HWC-CONTRACT-EDGE). Add a helper in `webcore_config_platform`
      that, given the `std::vector<bound_contract>` produced by `load_webcore_contracts` and an
      `ihttp_contract_edge&`, attaches every `validate`-mode document via `attach_validation` and
      submits every `drive`-mode document through the edge via `drive_contract(*document,
      edge.as_engine_submit())`, returning an aggregate summary (per-binding drive tallies + the
      edge's validate tally). Integration test: build an edge with `make_contract_edge(fake_engine)`
      where the fake engine returns a configurable response, load a `.pilcfg` carrying one
      `validate` and one `drive` contract through `load_webcore_contracts`, run the helper, and
      assert the drive contract produced the expected request traffic, a deliberately
      non-conforming response is tallied as a violation, and the attached validate document observed
      the crossings. Sub-second.
- [x] M-HWC-CONTRACTCFG-7 (umbrella — production live-edge wiring; now unblocked by PIL
      M-HWC-ENGINE-EDGE): wire bound contracts onto a *running* engine's synthetic HTTP edge so
      autonomous request/response traffic crossing the edge is auto-validated and drive contracts
      execute against the activated engine, instead of `webcore_config_platform::get_webcore`
      forwarding unchanged. Split into the items below.
- [x] M-HWC-CONTRACTCFG-7.1: Contract-wiring webcore decorator. Add a webcore decorator (in
      `webcore_config_platform`, alongside `wire_contracts_to_edge`) whose `activate` forwards to
      the underlying (configured) webcore, then — on the activated instance's
      `synthetic_http_edge()` — registers every `validate`-mode bound document as a
      `crossing_observer` that runs the document's `validate_request` / `validate_response` and
      tallies (a side diagnostic, D6 — never altering the engine), and drives every `drive`-mode
      bound document via `drive_contract(*document, m::pil::make_engine_submit(edge, timeout))`. The
      decorator's returned instance owns the registered observers / wiring for the activation's
      lifetime. If the activated instance exposes no synthetic edge (`synthetic_http_edge() ==
      nullptr`, e.g. the null engine), wiring is a tolerant no-op. `get_webcore` returns this
      decorator wrapping the configured engine (the intercepting webcore with synthetic mode
      enabled in production; the in-process engine in test). Reuse the bound-contract loading from
      CONTRACTCFG-3 and the validate/tally shape from CONTRACTCFG-6; no `.pilcfg` schema change.
- [x] M-HWC-CONTRACTCFG-7.2 (integration): build the config platform (`apply_webcore_config`) over
      an underlying platform whose `get_webcore` returns `m::pil::make_in_process_webcore(handler)`,
      where the handler returns a configurable (deliberately non-conforming) response; load a
      `.pilcfg` carrying one `validate` and one `drive` contract; activate through the config
      platform and assert (a) the drive contract produced the expected request traffic against the
      engine, (b) the non-conforming response is tallied as a violation, and (c) the `validate`
      document observed the live crossings via the registered observer (drive the engine with an
      independent autonomous request and confirm the observer validated it). Sub-second. Record in a
      note that the real-`hwebcore` path is the *same* decorator with the intercepting webcore over
      a real `hwebcore.dll` as the config-selected engine — the only element not exercised in CI is
      IIS itself (D-HWC-11 acknowledged limit).


## Moved 2026-06-19 — Alias object, sample lifecycle, value enumeration, fault config, redirected-notification test, and multi-arch SDK (M-ALIAS, M-SAMPLE, M-ENUMVALUE, M-FAULTCFG, M-FS-NOTIFY-REDIR, M-SDK)

### Milestone M-ALIAS — link-time Win32→mwin32 redirection ("alias object")

Goal: let unmodified client code that calls genuine Win32 registry functions
(`RegCreateKeyExW`, …) resolve **at link time** to the mwin32 shim
(`mRegCreateKeyExW`, …), with no source edits, no runtime patching, no kernel
work. The client opts in by adding one object/library to its link line. See
DESIGN-NOTES D8 for the mechanism and the advapi32 contract.

- [x] M-ALIAS-1 (spike — de-risk before generating all 84): Hand-author a
      single-function alias translation unit for `RegCloseKey` only, providing
      **both** symbol forms — the `/alternatename:RegCloseKey=mRegCloseKey`
      pragma (plain, non-`dllimport` reference) and an explicit `__imp_RegCloseKey`
      data-pointer definition (the `dllimport` reference the default `<windows.h>`
      declaration emits). Build a throwaway MSVC x64 test that (a) calls
      `RegCloseKey` via the default `<windows.h>` declaration and (b) calls it via
      a plain non-`dllimport` declaration, linking the shim import library
      `m_mwin32.lib` + this alias TU, and confirm both land in `mRegCloseKey`
      while `advapi32.lib` is **not** pulled for that symbol. Record the exact
      confirmed symbol spelling (in particular whether `__imp_RegCloseKey` is best
      defined as a `void*` initialized to `&mRegCloseKey` or chained to
      `__imp_mRegCloseKey`) — this spelling is the contract the generator emits.
      FINDINGS (recorded in DESIGN-NOTES D8): the `__imp_RegCloseKey` data-pointer
      definition (`extern "C" LSTATUS(APIENTRY* __imp_RegCloseKey)(HKEY) =
      &mRegCloseKey;`) is the decisive redirect and pulls no advapi32 conflict; the
      `/alternatename` pragma is only a weak fallback that **loses to advapi32**
      and so cannot be relied on when advapi32 is linked. Real `<windows.h>`
      clients always hit the `__imp_` path, so the generator emits both but the
      `__imp_` slot is the contract.

- [x] M-ALIAS-IMPORTLIB (re-plan, discovered during M-ALIAS-4; prerequisite for the
      generator to link in a consumer): Resolve the alias TU's undecorated shim
      references with a dedicated **undecorated import library** built from
      `mwin32.def`, leaving the shim source untouched. Discovery: the auto-generated
      `m_mwin32` import library exposes only the **decorated** C++ names
      (e.g. `?mRegCloseKey@@YAJPEAUHKEY__@@@Z`) because the `mReg*` functions have
      C++ linkage. The alias TU references undecorated names. The first alternative
      tried — giving the shim `extern "C"` linkage — was **abandoned**: under
      `/EHsc` the `mReg*` functions that re-throw `std::system_error` triggered
      C4297 ("extern C function assumed not to throw"), i.e. it would silently
      change the shim's exception contract. Instead, because the DLL's export table
      already carries the **undecorated** names (via the `.def`), a second import
      library built with `link /lib /def:mwin32.def /name:<dll>` exposes
      undecorated `mReg*` (and `__imp_mReg*`) symbols that bind to the same DLL at
      load time. The alias OBJECT library links this undecorated import lib (to
      resolve its references) plus the `m_mwin32` target (so consumers track and
      copy the DLL at runtime). No shim behavior changes.

- [x] M-ALIAS-2 (depends on M-ALIAS-1): Build the generator. A build-time step
      reads `mwin32.def`'s `EXPORTS` and emits `mwin32_alias.cpp` containing, for
      every exported `mReg<Name>`, an `__imp_Reg<Name>` data-pointer definition
      (the decisive redirect) plus a `/alternatename:Reg<Name>=mReg<Name>` pragma
      (the harmless fallback), targeting the Win32 name `Reg<Name>` (the mechanical
      map is "strip the leading `m`"). The `.def` is the single source of truth so
      the alias set and the export set can never drift. The generator must fail
      loudly if an export does not match the `mReg*` shape rather than silently
      skipping it. The slot is emitted signature-free as
      `extern "C" void mReg<Name>(); extern "C" void (*__imp_Reg<Name>)() =
      &mReg<Name>;` (the IAT slot is pointer-sized; the client casts through its
      own declared type at the call site). The undecorated `mReg*` references are
      resolved by the M-ALIAS-IMPORTLIB undecorated import library.

- [x] M-ALIAS-3 (depends on M-ALIAS-2): Add the `mwin32_alias` CMake **OBJECT**
      library target that clients link. An OBJECT library propagates its object
      files directly into the consumer's link (not pulled on demand like a static
      lib), which is required so the `__imp_` definitions are always present and
      preempt advapi32. It propagates the `m_mwin32` import library (so a client
      linking `mwin32_alias` gets the shim DLL's import lib transitively). Wire the
      generation into the build so the TU is regenerated when `mwin32.def` changes.

- [x] M-ALIAS-4 (depends on M-ALIAS-3): Link-proof integration test. A test
      executable that does **not** include `<m/mwin32/...>` and instead calls the
      genuine Win32 entry points (`RegCreateKeyExW`, `RegSetValueExW`,
      `RegQueryValueExW`, `RegCloseKey`) under a buffered `.pilcfg`, links
      `mwin32_alias`, and asserts the calls reached the shim (the write lands in
      the buffered overlay and the live registry is untouched). The genuine
      advapi32 `RegOpenKeyExW`, obtained via `GetProcAddress` (deliberately not
      redirected), confirms the live registry never saw the write. NOTE: a
      single-component subkey is used because the buffered overlay's `create_key`
      does not auto-create intermediate keys (it rejects multi-component paths via
      `has_parent_path()`); that buffered-layer gap is tracked separately.

- [x] M-ALIAS-5 (depends on M-ALIAS-4): Document client usage in `COMPONENT.md`
      (link `mwin32_alias`, what it redirects, the advapi32 limitation and the
      already-compiled-third-party-static-lib boundary) and finalize DESIGN-NOTES
      D8 with the confirmed symbol spelling from M-ALIAS-1.

### Milestone M-SAMPLE — sample client driving the capture/replay/logging lifecycle

Goal: a standalone sample program that calls genuine Win32 registry APIs, linked
against `mwin32_alias`, plus a harness that drives the full lifecycle the shim
enables — exercising the client with no effect on the real OS. Depends on M-ALIAS.

- [x] M-SAMPLE-1: Author the sample client (`sample/` under mwin32): a small
      program that performs a representative registry workload through genuine
      Win32 calls only (create a key, write several value types, read them back,
      enumerate, delete one) and reports what it observed. No mwin32 headers — it
      is an ordinary Win32 client that merely links `mwin32_alias`. NOTE: the shim
      stub `mRegEnumValueW` currently returns ERROR_NOT_SUPPORTED, so the sample's
      enumeration step degrades gracefully; the gap is tracked as M-ENUMVALUE below.

- [x] M-SAMPLE-2: Capture scenario. Run the sample under a buffered+persisted
      `.pilcfg` so its writes are captured into an in-memory overlay and persisted
      to a snapshot file, never touching the live registry. Assert via the snapshot
      that the expected keys/values were captured and that the live registry is
      unchanged.

- [x] M-SAMPLE-3: Replay scenario (mode (c)). Run the same sample against the
      snapshot captured in M-SAMPLE-2 (persisted_state `.pilcfg`) with no live
      underlying registry, and assert the client sees the captured state — the
      client runs identically without the real OS.

- [x] M-SAMPLE-4: Logging scenario. Run the sample under a record_modifications
      `.pilcfg` and assert the recorded modification log reflects the client's
      writes/deletes in order.

### Milestone M-ENUMVALUE — implement value enumeration in the shim (gap surfaced by M-SAMPLE-1)

- [x] M-ENUMVALUE-1: `mRegEnumValueW` / `mRegEnumValueA`
      (`src/Windows/libraries/mwin32/src/mwinreg.cpp`) are stubs returning
      ERROR_NOT_SUPPORTED, so a redirected client cannot enumerate values through
      the shim. Implement them against the PIL `ikey` value-enumeration surface
      (Win32 contract: fill `lpValueName`/`lpcchValueName`, optional `lpType` and
      `lpData`/`lpcbData`, return ERROR_NO_MORE_ITEMS past the end and
      ERROR_MORE_DATA on undersized buffers). Tests: enumerate the values written
      by the value-op tests in order; correct end and undersized-buffer behavior.

### Milestone M-FAULTCFG — fault injection selectable from .pilcfg (D8 fault layer)

Goal: make the PIL fault-injecting layer (already built in PIL: M-FAULT) selectable
through mwin32 configuration so the sample client can be driven through failure
paths. Depends on M-SAMPLE.

- [x] M-FAULTCFG-1: Expose the fault layer on the PIL public API — a way to
      construct a platform-interface that wraps an underlying stack with a fault
      script (parsed from XML or built programmatically), mirroring the existing
      `make_platform_interface` / `load_platform_interface` surface. Record the
      public shape in PIL DESIGN-NOTES.

- [x] M-FAULTCFG-2: Extend `.pilcfg` with an optional fault-script reference
      (path to a `<FaultScript>` file or inline rules) and wire
      `build_platform_from_config` / session to layer the fault platform when
      present. Strict parse, tolerant load (a broken fault config must not break
      the host), consistent with the existing `.pilcfg` decisions (D5/D7).

- [x] M-FAULTCFG-3: Fault scenario in the sample harness. Run the sample under a
      `.pilcfg` whose fault script makes (e.g.) the Nth `RegCreateKeyExW` fail with
      a chosen status, and assert the client observes the injected failure
      (mapped to the right `LSTATUS`) while other operations pass through.

### Milestone M-FS-NOTIFY-REDIR — redirected-directory notification integration test

Goal: prove that a watch registered on a redirected directory receives notifications
when the backing directory is mutated, with the notification path reported in the
public namespace. Enabled by PIL M-FS-MONITOR-REDIR-1 (path-shape reconciliation).

- [x] M-FS-NOTIFY-REDIR-1: Create a notification sample executable
      (`mwin32_notify_sample_client.cpp`) that opens a directory with
      `FILE_FLAG_BACKUP_SEMANTICS`, registers a watch via `ReadDirectoryChangesW`
      (aliased through mwin32), waits for a notification with a timeout, and
      reports the action + filename to stdout. Requires coordination mechanism
      (e.g. "ready" marker file) so the test can mutate the backing directory
      after the watch is armed.
- [x] M-FS-NOTIFY-REDIR-2: Add an integration test in `test_mwin32_sample.cpp` that:
      (a) creates a temp directory structure with backing and public subdirs,
      (b) writes a redirecting `.pilcfg` mapping the public prefix to the backing
          prefix,
      (c) launches the notification sample watching the public path,
      (d) waits for the "ready" marker,
      (e) mutates the backing directory (create a file),
      (f) asserts the sample reports a notification with the public path (not
          the backing path).

### Milestone M-SDK — publishable mwin32 SDK artifact (multi-arch, assembled by GitHub pipeline)

Goal: produce a standalone, downloadable **mwin32 SDK** as a release artifact — the
user's guide, the shim DLL + import libs + alias object for **both x64 and ARM64**,
the buildable examples, and a `find_package(m)` CMake package — assembled by a
GitHub Actions pipeline on tag push. The user's guide
([`docs/mwin32-sdk-guide.md`](docs/mwin32-sdk-guide.md)) is already authored;
the remaining work is packaging and the pipeline.

Design reference: see DESIGN-NOTES (new decision **D-SDK** to be recorded with
M-SDK-1) for the component layout and the CPack-component vs separate-package
choice. Existing release pipeline to extend:
[`.github/workflows/release.yml`](../../../../.github/workflows/release.yml)
(currently x64-only, single CPack zip).

- [x] M-SDK-1: Record decision **D-SDK** in
      [`DESIGN-NOTES.md`](DESIGN-NOTES.md): the SDK directory layout (§3 of the
      guide), the choice to ship per-architecture `bin/`+`lib/` subtrees under one
      package, that the SDK is built as a dedicated **CPack component**
      (`COMPONENT mwin32_sdk`) so it can be zipped independently of the full `m`
      release, and the rule that the bundled examples build against the *installed*
      package (`find_package(m)`), not the in-tree targets. Add the matching
      cross-reference from the PLANS.md row.
- [x] M-SDK-2: Tag the mwin32 install artifacts into a `mwin32_sdk` CPack
      component. In [`CMakeLists.txt`](CMakeLists.txt), give the `m_mwin32` /
      `mwin32_alias` install rules `COMPONENT mwin32_sdk`, install the public
      headers (`include/m/mwin32/*.h`) and
      [`docs/mwin32-sdk-guide.md`](docs/mwin32-sdk-guide.md) into the component, and
      lay the per-arch binaries under `${arch}/bin` and `${arch}/lib`. Verify a
      local `cpack -D CPACK_COMPONENTS_ALL=mwin32_sdk` (x64) produces the §3 layout
      for the current architecture.
- [x] M-SDK-3: Package the examples as standalone, installed sources. Install the
      three [`sample/`](sample) clients plus a generated top-level
      `examples/CMakeLists.txt` that does `find_package(m CONFIG REQUIRED)` and
      links `m::mwin32_alias`, into the `mwin32_sdk` component under `examples/`.
      Verify the installed example tree configures and builds against the installed
      package out-of-tree (x64).
- [x] M-SDK-4: Multi-arch assembly merge step. Add a CMake/CTest-driven (or script)
      step that takes an x64 install tree and an ARM64 install tree (each produced
      by a separate configured build) and merges them into the single SDK layout
      (§3): shared `include/`, `lib/cmake/`, `docs/`, `examples/`, with arch-specific
      `x64/` and `arm64/` binary subtrees. The alias import-lib generation in
      [`CMakeLists.txt`](CMakeLists.txt) currently hard-codes `/machine:x64`; make it
      follow the active target architecture so the ARM64 build produces a correct
      ARM64 alias import lib. Verify the merged tree matches §3.
- [x] M-SDK-5 (integration): GitHub pipeline assembles and publishes the SDK. Add a
      job (extend [`.github/workflows/release.yml`](../../../../.github/workflows/release.yml)
      or a sibling `mwin32-sdk.yml`) that, on the same `v*` tag trigger, builds the
      `mwin32_sdk` component for **x64** and **ARM64** (matrix), runs the in-scope
      mwin32 tests for the buildable arch, runs the M-SDK-4 merge, names the zip
      `mwin32-sdk-<tag>.zip`, and attaches it to the GitHub Release alongside the
      existing full-`m` zip. Document the cut-a-release steps in the workflow header.
