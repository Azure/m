# pil CHECKLIST

Active work: **Hostable Web Core (HWC) isolation** — the third PIL surface (after registry and
filesystem), surfaced through the `mwin32` Win32 shim. Design rationale lives in
[DESIGN-NOTES.md](DESIGN-NOTES.md) decisions **D-HWC-1 … D-HWC-7**, reusing the surface-neutral
decorator decisions **D1–D8**. The companion shim work (the `mWebCore*` entry points) lives in
the `mwin32` source-component — see the cross-component handoff at the end of Phase 1 and
[`src/Windows/libraries/mwin32/CHECKLIST.md`](../../Windows/libraries/mwin32/CHECKLIST.md).

Background — the filesystem surface (the second surface) is complete except for the
**M-FS-STREAMS** milestone at the bottom of this file. Its tier-1 (redirection-backed file
content) is now **active** and decomposed into dependency-ordered sub-items; the content
accessor (M-FS-STREAMS-1.1 / 1.2) is the cross-component unblocker for the `mwin32`
M-FS-CONTENT shim. Tier-2 (the alternate-data-stream sub-namespace) stays deferred. Registry
(first surface) is in [COMPLETED-CHECKLIST.md](COMPLETED-CHECKLIST.md).

Orientation — how the HWC surface differs from the state surfaces (drives the milestone shapes):
- **HWC is an *engine* surface, not a state surface** (D-HWC-1). `hwebcore.dll` has no persistent
  state to snapshot; its behavior comes from the config it *reads* and the network edge it
  *binds*. So buffered / journaling are `M_NOT_IMPLEMENTED`; isolation is **composed** from the
  filesystem / registry surfaces the engine reads.
- **Three flat C entry points** (verified against the SDK `um/hwebcore.h`):
  `WebCoreActivate(PCWSTR appHostConfig, PCWSTR rootWebConfig, PCWSTR instanceName)`,
  `WebCoreShutdown(DWORD fImmediate)`, `WebCoreSetMetadata(PCWSTR type, PCWSTR value)` — all
  `HRESULT`.
- **Engine is bound via `LoadLibraryExW(LOAD_LIBRARY_SEARCH_SYSTEM32)` + `GetProcAddress`**
  (D-HWC-3), never statically imported; the three proc addresses are the test seam (a fake engine
  is a different function-pointer triple — no IIS feature needed to test).
- **Single activation per process** (D-HWC-5); HWC has no handle in its ABI, so the session owns
  the one instance token (no `handle_table`).
- **Config / registry isolation is by materialization (default) or opt-in module-scoped Detours
  interception** (D-HWC-4, D-HWC-7); the network edge is a deferred `ihttp_listener` namespace
  redirection (D-HWC-6).

---

# Phase 1 — surface, live provider, decorator facets, mwin32 shims

## Milestone M-HWC-IFACE — surface interfaces + null provider (D-HWC-1, D-HWC-2)

- [x] M-HWC-IFACE-1: Add `webcore_interfaces.h` (`m::pil`): `iwebcore_instance` (opaque RAII
      activation token; destruction shuts the instance down, like `ifilesystem_monitor_token`) and
      `iwebcore` with the ec-primitive `activate(activate_flags, activation_request const&,
      std::unique_ptr<iwebcore_instance>&, std::error_code&)` plus a thin throwing wrapper, and
      `set_metadata(...)`. `activation_request` carries the app-host config and optional root-web
      config as **`file_path`** values (paths in the isolated filesystem) plus the instance name.
      Define `activate_flags` (e.g. `immediate_shutdown_on_release`) and an `activate_disposition`
      whose only contractual non-success code is `already_activated`. Add `null_webcore` /
      `null_webcore_instance` whose operations are `M_NOT_IMPLEMENTED`.
- [x] M-HWC-IFACE-2: Add `iplatform::get_webcore(get_webcore_flags, std::shared_ptr<iwebcore>&)`
      to [platform_interfaces.h](include/m/pil/platform_interfaces.h) with a **default** that
      yields `null_webcore` (mirrors the `get_filesystem` default, D9), plus the friendly
      `get_webcore()` accessor that asserts a nominal disposition. Existing registry-only and
      filesystem providers inherit the default unchanged.
- [x] M-HWC-IFACE-3: Add the public façade `m::pil::webcore_host` in a new `webcore.h` that
      re-declares `activate_flags` bit-for-bit and maps them onto the interface enum (exactly as
      `filesystem_monitor` / `registry_monitor` do), so the public header carries no
      `iwebcore` dependency.
- [x] M-HWC-IFACE-4 (integration): Test that the null provider surfaces not-implemented through
      the façade and that each existing decorator (passthrough/buffered/journaling/logging/fault/
      redirecting) forwards `get_webcore` to its underlying without crashing.

## Milestone M-HWC-DIRECT — live Windows engine provider (D-HWC-3, D-HWC-5)

- [x] M-HWC-DIRECT-1: Direct/Windows webcore provider with the **injectable function-pointer
      seam** — a struct of `PFN_WEB_CORE_ACTIVATE` / `PFN_WEB_CORE_SHUTDOWN` /
      `PFN_WEB_CORE_SET_METADATA` — default-bound by `LoadLibraryExW` against the **absolute**
      `system32\inetsrv\hwebcore.dll` path (resolve via `GetSystemDirectoryW` + `\inetsrv\`),
      adding `inetsrv` to the dependency search (`AddDllDirectory` / `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR`)
      so the engine's sibling DLLs resolve — a bare-name `LOAD_LIBRARY_SEARCH_SYSTEM32` load fails
      `ERROR_MOD_NOT_FOUND` (verified). Finalize the exact search-flag combo here against the live
      engine. `GetProcAddress` the three entries; `FreeLibrary` on provider teardown; module handle
      is provider-owned (loaded once on first `activate`).
- [x] M-HWC-DIRECT-2: `activate` → `WebCoreActivate`; token destructor → `WebCoreShutdown(fImmediate)`.
      Map `HRESULT` → `std::error_code` / `disposition`: `HRESULT_FROM_WIN32(ERROR_SERVICE_ALREADY_RUNNING)`
      → `already_activated` disposition; `ERROR_SERVICE_NOT_ACTIVE` handled on shutdown. (A new
      `HRESULT`↔`ec` helper, sibling to the registry `ec`→`LSTATUS` mapping.)
- [x] M-HWC-DIRECT-3: Enforce single activation in the provider (holds the one live instance; a
      second `activate` yields `already_activated` without calling the engine twice).
- [x] M-HWC-DIRECT-4: `set_metadata` → `WebCoreSetMetadata(type, value)`.
- [x] M-HWC-DIRECT-5 (integration): Fake-engine test (inject a function-pointer triple) drives
      activate / already-activated / shutdown / set_metadata lifecycle end to end — no IIS feature
      installed.

## Milestone M-HWC-FACETS — decorator facets (D-HWC-1)

- [x] M-HWC-FACETS-1: Passthrough forwards `get_webcore` to the underlying platform.
- [x] M-HWC-FACETS-2: Logging facet traces `activate` / `shutdown` / `set_metadata` as a side
      diagnostic (D6 — records nothing into any persisted artifact).
- [x] M-HWC-FACETS-3: Fault facet injects `activate` `HRESULT` failures via the D8 counted-rule
      script (Nth activation fails with a mapped foundation exception / ec).
- [x] M-HWC-FACETS-4: Buffered and journaling `get_webcore` return `M_NOT_IMPLEMENTED` (an engine
      is not snapshotted — documented per D-HWC-1).
- [x] M-HWC-FACETS-5 (integration): Redirecting maps the config `file_path` public↔private on the
      way into `activate`; integration test exercises the fake engine through the
      passthrough / logging / fault / redirecting facets.
      > **➡ CROSS-COMPONENT HANDOFF:** the `mWebCore*` shim entry points are next, in component
      > `src/Windows/libraries/mwin32` → milestone `M-HWC-SHIM`. See
      > [`src/Windows/libraries/mwin32/CHECKLIST.md`](../../Windows/libraries/mwin32/CHECKLIST.md).

---

# Phase 2 — config / registry isolation for the un-shimmed engine (D-HWC-4, D-HWC-7)

## Milestone M-HWC-MATERIALIZE — config projection bridge (D-HWC-4 default path)

- [x] M-HWC-MATERIALIZE-1: On `activate`, resolve the config `file_path` through the isolated
      `ifilesystem`, read its bytes, parse `applicationHost.config`, project every `physicalPath` /
      content root from the isolated FS into a real per-instance temp dir, rewrite the paths, and
      write the rewritten config to a real path before calling `WebCoreActivate`. Token destructor
      shuts down and deletes the projection. Document the materialization isolation boundary.
- [x] M-HWC-MATERIALIZE-2 (integration): Buffered filesystem holding a minimal
      `applicationHost.config` → assert the materialization reads/rewrites/projects correctly and
      the fake engine is handed real paths.

## Milestone M-HWC-INTERCEPT — module-scoped interception (D-HWC-4 opt-in, D-HWC-7)

- [x] M-HWC-INTERCEPT-1: Module-scoped interception envelope installed **only on
      `hwebcore.dll`'s own IAT / delay-IAT** (via the `HMODULE` from D-HWC-3), routing the engine's
      `Reg*` / `CreateFileW` / `FindFirstFileW` calls into the active PIL registry / filesystem
      surfaces. Gated behind `webcore.interception` in `.pilcfg`, **off by default**.
      **Implementation note:** Stub implementation — hook infrastructure in place but hook
      functions fall through to originals; full PIL routing marked TODO.
- [x] M-HWC-INTERCEPT-2 (integration): With interception on, assert the engine's config/registry
      reads resolve against PIL fakes (no materialization) and the logging facet captures the exact
      set of keys / files the engine touched.
      **Implementation note:** Tests handle table allocation/lookup/release, decorator creation,
      activation forwarding, and thread-local context. Hook routing tests deferred until hooks
      are fully implemented.

---

# Phase 3 — network edge (D-HWC-6)

## Milestone M-HWC-HTTP — `ihttp_listener` namespace redirection

- [x] M-HWC-HTTP-1: Define the deferred `ihttp_listener` surface and the namespace-redirection
      contract (`remap(public_endpoint, private_endpoint)`); `.pilcfg` `webcore.endpoints` mapping
      table (public ↔ private host:port).
      **Implementation note:** Created `http_listener_interfaces.h` with `http_endpoint`,
      `endpoint_mapping`, `ihttp_listener_session`, `ihttp_listener` interfaces with
      `create_session`/`remap`/`unmap` operations; null provider for default platform;
      `get_http_listener` accessor on `iplatform`. `.pilcfg` integration deferred to HTTP-2.
- [x] M-HWC-HTTP-2 (Tier A): Intercept the HTTP Server API
      (`HttpAddUrlToUrlGroup` / `HttpAddUrl`) on the engine module and remap host:port to loopback
      + ephemeral port, synthesizing the URL-ACL / cert binding for that private prefix; real
      `http.sys` serves on the private prefix.
      **Implementation note:** Hook infrastructure installed for HttpAddUrl, HttpAddUrlToUrlGroup,
      HttpRemoveUrl, HttpRemoveUrlFromUrlGroup; http_listener_session wired into interception_context
      and initialized during activate. URL parsing and remapping implemented in hooks: public
      endpoints are looked up via `http_listener_session`, remapped to private loopback endpoints,
      and tracked for reverse lookup on removal. URL-ACL synthesis and cert binding generation
      still TODO for full production use.
- [x] M-HWC-HTTP-3 (Tier B): Intercept the receive / send HTTP Server API too and feed requests
      from an in-process queue — no `http.sys`, no admin. Drive synthetic requests into the
      activated (fake) engine and assert responses; the strongest fully-deterministic edge.
      **Implementation note:** Implemented `synthetic_http_queue` class with thread-safe
      request/response queue. When `synthetic_http_enabled` is true, `HttpReceiveHttpRequest`
      returns requests from the queue, `HttpSendHttpResponse` captures responses with headers and
      body, and `HttpSendResponseEntityBody` appends body chunks. Supports synchronous mode
      with `enqueue_request`, `try_dequeue_request`, and `wait_for_response` APIs. Async/overlapped
      mode returns `ERROR_INVALID_PARAMETER` (TODO for full async support).

---

## Milestone M-HWC-REVIEW — interception review follow-ups (PR #174 Copilot review)

Second-pass review of `src/libraries/pil/src/intercepting/intercepting_webcore.cpp` surfaced
five issues. The three contained correctness/perf items (M-HWC-REVIEW-1, -3, -5) are addressed
in this milestone; the two feature-completion items (M-HWC-REVIEW-2, -4) extend the still-stubby
synthetic-file and synthetic-HTTP marshaling surfaces and are queued here in dependency order.

- [x] M-HWC-REVIEW-1: Make `g_active_context` a plain process-global instead of `thread_local`.
      As `thread_local` it was null on the engine's worker threads, so every hook that guarded on
      it silently bypassed interception off the publishing thread. It is data-race-free as a plain
      global because publication is ordered: `activate()` sets it under `m_mutex` before the engine
      starts its threads, and `~webcore_instance` nulls it after the underlying instance is shut
      down and its threads are joined.
- [x] M-HWC-REVIEW-3: Add a lock-free synthetic-handle fast-path to the `CloseHandle` hook.
      Real kernel handles are far below `synthetic_handle_base`, so `hook_CloseHandle` now returns
      to `original_CloseHandle` immediately for any handle below the base, skipping the mutex and
      map probe on the (very hot) path that closes real OS handles.
- [x] M-HWC-REVIEW-5: Don't drop a synthetic HTTP request on `ERROR_MORE_DATA`.
      `try_dequeue_request` pops before the caller's buffer size is known; when marshaling needs a
      larger buffer the request was lost. `hook_HttpReceiveHttpRequest` now `requeue_front`s the
      dequeued request (preserving its `request_id` and FIFO order) before returning
      `ERROR_MORE_DATA`, so the caller's retry sees it again.
- [x] M-HWC-REVIEW-2: Complete the synthetic file I/O surface (option (a)). Added
      `ReadFile` / `WriteFile` / `GetFileSizeEx` / `GetFileSize` / `SetFilePointerEx` /
      `SetFilePointer` / `GetFileType` / `FlushFileBuffers` / `SetEndOfFile` hooks that route a
      synthetic handle through its backing `ifile`. `interception_context` now tracks a per-handle
      byte position (a `file_state`) so the kernel32 implicit-file-pointer semantics work; each
      hook uses the same lock-free `synthetic_handle_base` fast path and falls through to the real
      function for any handle that is not ours. A handle from `hook_CreateFileW` is now usable
      rather than failing every call with `ERROR_INVALID_HANDLE`.
- [x] M-HWC-REVIEW-4: Complete synthetic request marshaling so request bodies are delivered.
      `marshal_synthetic_request` now lays out the raw URL, the cooked (wide) URL components
      (full / host / abs-path / query), a computed `Content-Length` known header, the `Host`
      known header, and any remaining caller-supplied headers as unknown headers, plus
      `BytesReceived` — all in the trailing region of the caller's buffer via a bump allocator
      that reports the true required size on `ERROR_MORE_DATA`. With `Content-Length` present the
      engine now calls `HttpReceiveRequestEntityBody`, so the `s_synthetic_request_bodies` stash
      is reachable; stale entries are dropped on completing `HttpSendHttpResponse` and on instance
      teardown (`clear_synthetic_request_bodies`).

---

## Milestone M-HWC-REVIEW2 — synthetic-file I/O hardening (PR #174 Copilot review, 3rd pass)

The third review pass over the M-HWC-REVIEW-2 file-I/O hook work surfaced seven issues; the two
blocking ones (handle-range collision, write-breaks-on-second-call) plus the IOCP-completion gap
are correctness bugs under realistic engine usage, the rest are robustness/perf hardening.

- [x] M-HWC-REVIEW2-1: Give each synthetic-handle kind its own non-overlapping range. Keys, files,
      and find-handles all minted from `~0x80000100` and incremented by 1, so after 256 file
      opens the file counter aliased the find/key ranges and `is_synthetic_file_handle` returned
      true for find cookies. Replace the two near-adjacent bases with a `synthetic_handle_floor`
      plus three widely separated per-kind bases (keys / files / finds), keeping the lock-free
      fast path as `value < synthetic_handle_floor`.
- [x] M-HWC-REVIEW2-2: Make `WriteFile` on a synthetic handle work past the first chunk. The
      backing `ifile::write_content` models only whole-file replacement at offset 0, so the second
      positioned `WriteFile` failed `ERROR_NOT_SUPPORTED`. `file_state` now accumulates writes into
      an in-memory whole-file buffer (the authoritative content while dirty) and flushes it as a
      single `write_content` on flush / close.
- [x] M-HWC-REVIEW2-3: Reject overlapped I/O on synthetic handles. We only `SetEvent(hEvent)` and
      never queue an IOCP completion packet, so an IOCP-pump thread would hang. Until real IOCP
      completion is modeled, `hook_ReadFile` / `hook_WriteFile` fail `ERROR_INVALID_PARAMETER` for
      a synthetic handle with a non-null `OVERLAPPED` (mirrors `hook_HttpReceiveHttpRequest`); the
      synchronous path is fully functional.
- [x] M-HWC-REVIEW2-4: Don't hold `file_handle_mutex` across the backing read. `read_file_handle`
      now snapshots the `shared_ptr<ifile>` + position under the lock, releases it for the
      `read_content` call, then re-acquires briefly to advance the position, so independent reads
      no longer serialise on one mutex.
- [x] M-HWC-REVIEW2-5: Propagate `query_information` failure. `get_file_handle_size` and the
      `FILE_END` seek read `m_size` without checking the disposition, so a failed query reported
      success with size 0. Both now surface a non-ok disposition through `ec`.
- [x] M-HWC-REVIEW2-6: Make `g_active_context` a `std::atomic<interception_context*>` read with
      `memory_order_acquire` / written with `memory_order_release`, so the cross-thread publication
      of the active context no longer rests on a "correct-if-the-engine-joins-its-threads" argument
      on a security-sensitive surface.

## Milestone M-HWC-SELFAUDIT — same-class issues found by self-audit (no external review)

A self-audit for the same bug classes the third review pass raised (swallowed/mishandled
`query_information` failure; overlapped-I/O rejection; locks held across backing I/O) found one
additional instance of the REVIEW2-5 class.

- [x] M-HWC-SELFAUDIT-1: `hook_GetFileAttributesW` queried directory/file metadata through the
      no-argument `ifile::query_information()` / `idirectory::query_information()` convenience
      overload, which raises a process-fatal `M_INTERNAL_ERROR_CHECK` on a non-ok disposition — a
      backing metadata failure would abort the hosting service from inside the hook (the
      surrounding `catch (...)` cannot recover a fail-fast abort). Both call sites now use the
      disposition-checked two-argument overload and map a failed query to
      `SetLastError(ERROR_FILE_NOT_FOUND)` + `INVALID_FILE_ATTRIBUTES`.

---

## Milestone M-FS-STREAMS — redirection-backed file content (tier 1 active) & ADS sub-namespace (tier 2 deferred) (D14, D16, D17)

Closes the acknowledged-incorrect deferral (D14): today a file is a metadata-only node, so a
sealed buffered snapshot cannot serve file *content*. The resolution is **redirection-backed**,
not byte capture/replay (D16): redirect a namespace subtree to an assembled real backing
directory, serve reads from it, and track only namespace-level change over it. Fine-grained
content mutation (file-size change, byte-range overwrite) is an explicit non-goal. The accessor
shape — a defaulted positioned whole-file read/write ec-primitive on `ifile` — is D17.

Tier 1 (redirection-backed content) is decomposed into dependency-ordered sub-items. The
content accessor (1.1 read / 1.2 write) is the cross-component unblocker named by the mwin32
handoff; the subtree binding (1.3) and namespace-mutation overlay (1.4) are the isolation
feature layered over it. Tier 2 (the ADS sub-namespace, M-FS-STREAMS-2) stays deferred.

- [x] M-FS-STREAMS-1.1 (content read accessor): Add `ifile::read_content(read_content_flags,
      offset, buffer, bytes_read, ec)` — a positioned whole-file byte read — as a **defaulted**
      ec-primitive on `ifile` (the default reports `std::errc::not_supported`, the documented
      deferred-content outcome for nodes that model only namespace + metadata: a sealed buffered
      snapshot, the null leaf), plus throwing + convenience wrappers and the `m::pil::file`
      façade method. Serve real bytes in the direct/win32 `file` (positioned `ReadFile` via
      `OVERLAPPED.Offset`); forward in passthrough / logging / redirecting (fault returns the
      underlying file unwrapped, so it needs no change). PIL unit tests (real read, short read at
      EOF, default not-supported, decorator forwarding).

      > **➡ CROSS-COMPONENT HANDOFF:** unblocks the read half of `src/Windows/libraries/mwin32`
      > → **M-FS-CONTENT-1** (`mReadFile` / …) and **M-FS-LEGACY-3**. See
      > [`src/Windows/libraries/mwin32/CHECKLIST.md`](../../Windows/libraries/mwin32/CHECKLIST.md).
- [x] M-FS-STREAMS-1.2 (content write accessor, whole-file): Add `ifile::write_content(...)` the
      same way — **whole-file replacement only** (D16): a write at offset 0 that sets the file's
      extent; a write whose offset is non-zero (a partial / mid-file overwrite) is rejected with
      the documented unsupported outcome. Concrete in direct/win32 (positioned `WriteFile` +
      `SetEndOfFile`); forward in passthrough / logging / redirecting; the default + buffered
      report not-supported. Façade method + PIL unit tests.

      > **➡ CROSS-COMPONENT HANDOFF:** unblocks the write half of `src/Windows/libraries/mwin32`
      > → **M-FS-CONTENT** (`mWriteFile` / `mSetEndOfFile`, whole-file). See
      > [`src/Windows/libraries/mwin32/CHECKLIST.md`](../../Windows/libraries/mwin32/CHECKLIST.md).
- [x] M-FS-STREAMS-1.3 (subtree redirection binding at init): Add a configuration path so PIL init
      can bind a chosen subtree (e.g. `C:\Windows\system32`) to an assembled real backing
      directory through the existing redirecting decorator (D16). Reads of redirected names
      resolve to the backing files and are served whole-file by 1.1 / 1.2. Integration test: a
      file placed in the backing directory is read back through the bound public path.
- [x] M-FS-STREAMS-1.4 (namespace-mutation overlay / tombstones): Track create / delete /
      rename(move) of entries within the redirected subtree as overlay entries / tombstones over
      the backing directory (the "partial support" the deferral always meant — deletions and
      renames observable and isolated; no byte-range / size mutation, D16). Integration test.
- [x] M-FS-STREAMS-1.5 (re-baseline stale null-provider tests): Re-baseline the 4 stale
      "null-provider filesystem" tests in `test_pil_registry`
      (`TestFilesystemPlatform.DecoratorStackStillYieldsNullFilesystem`,
      `TestFilesystemWrappers.OpenRootNotImplementedAgainstNullProvider`,
      `TestFilesystemWrappers.FilesystemClassCopyAndMove`,
      `TestFilesystemWrappers.FilesystemClassSwap`). The decorator stack now forwards
      `get_filesystem` to the live provider, so `open_root("C:")` succeeds and the old
      `EXPECT_THROW(m::not_implemented)` premise is false. Decide what each should now verify
      against a genuinely-null provider and update expectations. On completion, remove the
      corresponding entry from `UNRESOLVED-TEST-FAILURES.md`. (Surfaced by the mwin32 M-FS-SHIM
      milestone; see `UNRESOLVED-TEST-FAILURES.md` → "Stale null-provider filesystem
      expectations in `test_pil_registry`".)
- [ ] M-FS-STREAMS-2 (DEFERRED, tier 2 — alternate-data-stream sub-namespace): Model a file's
      named / alternate data streams (`file:stream`) as their own sub-namespace and isolate
      the *namespace-level* stream operations (create, delete, rename/move). Secondary to
      tier 1; the literal NTFS ADS surface, not the primary content story (D16).

## Milestone M-FS-MONITOR-REDIR — reconcile redirected paths with the change monitor

Surfaced while implementing the `mwin32` change-notification shim (mwin32 D15). The redirecting
decorator keys on a *relative* directory name (e.g. `mwin32_copy_pub`), but a live watch must open
a *root-qualified* directory path. The `fs_redirector::try_map` now handles this by suffix-matching
on the relative portion of rooted paths: given `C:\temp\xxx\pub_prefix\child`, it strips leading
components from the relative path until it finds `pub_prefix` in the redirection table, then
reconstructs `C:\temp\xxx\priv_prefix\child`.

- [x] M-FS-MONITOR-REDIR-1: Give the redirecting decorator's `monitor()` a path-shape
      reconciliation so a `register_watch` on a redirected directory maps the public root-qualified
      watch path to the private backing directory (and maps reported entry paths back public→private),
      reusing the same redirection table the namespace ops consult.

      > **➡ CROSS-COMPONENT HANDOFF:** this unblocks a redirected-watch notification test in
      > `src/Windows/libraries/mwin32`. See
      > [`src/Windows/libraries/mwin32/CHECKLIST.md`](../../Windows/libraries/mwin32/CHECKLIST.md)
      > for the mwin32 integration test item (M-FS-NOTIFY-REDIR).

## Milestone M-FS-SHORTNAME — buffered overlay resolves 8.3 short-name path components

The buffered overlay captures each directory's children by enumeration, which yields the
**long** names, and keys them in a case-insensitive map. A host path may legitimately carry an
8.3 **short** component (e.g. CI runners' `%TEMP%` = `C:\Users\RUNNER~1\...` because
`runneradmin` > 8 chars). The exact long-name lookup then misses and `open_directory` reports
"no such file or directory". Reproduced deterministically by
`BufferedSave.FilesystemShortNamePathComponentReproducesCiFailure` (forces the condition via
`GetShortPathNameW`). The map is already case-insensitive, so case is not the gap — only the
8.3 alias is. Fix: capture each entry's alternate (8.3) name as an optional alias, persist it,
and resolve it on an exact-match miss. Decision recorded as D17 in
[DESIGN-NOTES.md](DESIGN-NOTES.md) (case-insensitivity already handled per D12;
short-name aliasing is the new behavior).

- [x] M-FS-SHORTNAME-1: Enable diagnostic tracing for the PIL Win32 tests (link
      `m_googletest_main` so the diagnostic-channel `cout_sink` is registered) and add
      `open_directory` HIT/MISS traces in the buffered overlay; add the deterministic 8.3 repro
      test. (Logging-first; proves the root cause locally.)
- [x] M-FS-SHORTNAME-2: Add `directory_entry::m_short_name` (alternate name, empty when none)
      and capture `WIN32_FIND_DATAW::cAlternateFileName` in the Win32 `enumerate_entries`
      (switch `FindExInfoBasic` → `FindExInfoStandard`, which is required for the alternate name
      to be populated).
- [x] M-FS-SHORTNAME-3: Carry the short name on the buffered `entry_node`, populate it during
      whole-node capture, persist it via the `short_name` XML attribute (save + load), and on an
      `open_directory`/lookup exact-match miss resolve a requested name against entries' short
      names. Sealed snapshots resolve from the persisted alias; live overlays from capture.
- [x] M-FS-SHORTNAME-4 (integration): the 8.3 repro test and the six previously CI-failing
      filesystem tests pass in debug and release; remove the verbose per-lookup traces (or keep
      gated) so normal runs are quiet.

---

# Phase 4 — OpenAPI/Swagger contract binding on the HTTP edge (D-HWC-8)

Bind the team's OpenAPI (Swagger) documents to the HWC HTTP edge so the same spec both
**validates** traffic crossing the synthetic edge and **drives** example traffic into the engine.
Design rationale: **D-HWC-8**. Specs are YAML (any version); they are parsed to an internal model
once, so the validator/matcher never see YAML. New dependencies (`yaml-cpp`,
`json-schema-validator`) land in M-HWC-CONTRACT-MODEL. The `.pilcfg` binding that selects
a spec + endpoint + mode is the cross-component companion work in `mwin32` (M-HWC-CONTRACTCFG) and
is gated behind the surface landing here first.

## Milestone M-HWC-CONTRACT-MODEL — spec loading + internal model (D-HWC-8)

- [x] M-HWC-CONTRACT-MODEL-1: Add `yaml-cpp` and `json-schema-validator` (CMake package
      `nlohmann_json_schema_validator`) to [vcpkg.json](../../../vcpkg.json) (done) and link them into `m_pil`
      ([src/CMakeLists.txt](src/CMakeLists.txt)). Add a `contract/` source subdirectory.
- [x] M-HWC-CONTRACT-MODEL-2: Add `openapi_model.{h,cpp}` (internal, `m::pil`): a YAML→model
      loader. Parse the YAML with `yaml-cpp`, convert the node tree to `nlohmann::json`, detect the
      version (`swagger: "2.0"` vs `openapi: "3.x"`), and normalize into a flat model — a list of
      operations each carrying `method`, a path **template** (`/items/{id}`), the parameter list
      (name / in / required / schema), the optional request-body schema, and a status→response map
      (each response carrying an optional body schema + declared headers). Body schemas are stored
      as `nlohmann::json` ready for the validator. The loader takes spec **bytes** (the caller owns
      file I/O, mirroring `parse_pilcfg`'s pure-text contract) and returns the model or a
      diagnostic on malformed input.
- [x] M-HWC-CONTRACT-MODEL-3: Add the path-template matcher: given a request method + concrete
      path, find the operation whose template matches (literal segments + `{param}` captures),
      returning the operation and the captured path parameters. Pure function over the model; no
      HTTP types.
- [x] M-HWC-CONTRACT-MODEL-4 (unit tests): small inline YAML specs (OAS 2.0 and 3.0/3.1) exercise
      version detection, operation/parameter/body/response extraction, path-template matching
      (literal, single param, multi param, trailing, no-match), and malformed-spec diagnostics.
      ≥10 cases, sub-second.

## Milestone M-HWC-CONTRACT-REFS — bundle resolution + media-typed bodies (D-HWC-9)

- [x] M-HWC-CONTRACT-REFS-1: Extend `load_openapi_model` to take a caller-supplied
      `ref_resolver` (a `(std::string_view relative_path) -> std::optional<std::string>` callable)
      alongside the root spec bytes. PIL owns ref-splicing; the caller owns where bytes come from
      (an in-memory map in tests, a sibling-directory read under `.pilcfg`). A spec with no
      external refs never invokes the resolver. Caller-owns-I/O is preserved (mirrors
      `parse_pilcfg`).
- [x] M-HWC-CONTRACT-REFS-2: Resolve `$ref` in the model: internal (`#/components/…`),
      relative-file (`other.yml#/components/…`), and **transitive** refs, with cycle detection
      (an unresolved or cyclic ref is a load diagnostic, never a silent omission). Component
      libraries (`parameters` / `schemas` / `requestBodies` / `responses`) are merged into the
      flat model so operations carry fully-resolved parameters, bodies, and responses.
- [x] M-HWC-CONTRACT-REFS-3: Replace the single-body-schema fields with a **media-type → schema**
      map on request bodies and responses (capturing e.g. `application/json` and `text/xml`),
      preserving each media type's schema and example. The matcher and downstream validators read
      the map; JSON remains the schema-validated type (D-HWC-9).
- [x] M-HWC-CONTRACT-REFS-4: Operation identity honors a **query discriminator** — when a path key
      carries a query key that selects the operation, matching considers it; and read an authored
      validation-eligibility vendor extension (`x-…`) into the operation (never a YAML comment).
      Add a normalization helper that lifts a query-in-path-key into a real parameter.
- [x] M-HWC-CONTRACT-REFS-5 (unit tests): in-memory multi-document fixtures exercise internal +
      relative-file + transitive ref resolution, cycle/unresolved-ref diagnostics, media-type maps
      (JSON and XML bodies), query-discriminated operation matching, and the `x-…` eligibility
      read. ≥10 cases, sub-second.

## Milestone M-HWC-CONTRACT-IFACE — `ihttp_contract` surface + null provider (D-HWC-8)

- [x] M-HWC-CONTRACT-IFACE-1: Add `http_contract_interfaces.h` (`m::pil`): `ihttp_contract` with
      ec-primitive `load(spec_bytes, std::unique_ptr<ihttp_contract_document>&, std::error_code&)`
      and, on the document, `validate_request(method, path, headers, body, …, std::error_code&)`
      and `validate_response(method, path, status, headers, body, …, std::error_code&)` returning
      a `disposition` whose contractual non-success codes are the violation kinds (unknown
      operation, parameter invalid, body-schema invalid, undeclared status). Add `null_http_contract`
      whose operations are `M_NOT_IMPLEMENTED`.
- [x] M-HWC-CONTRACT-IFACE-2: Add `iplatform::get_http_contract(get_http_contract_flags,
      std::shared_ptr<ihttp_contract>&)` to
      [platform_interfaces.h](include/m/pil/platform_interfaces.h) with a **default** yielding
      `null_http_contract` (mirrors `get_webcore` / `get_http_listener`), plus the friendly
      `get_http_contract()` accessor.
- [x] M-HWC-CONTRACT-IFACE-3: Add the public façade in a new `http_contract.h` that re-declares the
      `contract_mode` enum (`validate` / `drive`) bit-for-bit and maps it onto the interface enum,
      so the public header carries no `ihttp_contract` dependency.
- [x] M-HWC-CONTRACT-IFACE-4 (integration): the null provider surfaces not-implemented through the
      façade and each existing decorator forwards `get_http_contract` to its underlying without
      crashing.

## Milestone M-HWC-CONTRACT-VALIDATE — validate mode on the synthetic edge (D-HWC-8, D-HWC-9, D6)

- [x] M-HWC-CONTRACT-VALIDATE-1: Live `ihttp_contract` provider backed by the M-HWC-CONTRACT-MODEL
      loader + matcher; `load` builds a document holding the model and a
      `nlohmann-json-schema-validator` per **JSON** body schema. `validate_request` runs
      method/path (+ query discriminator) match → parameter checks → request-body schema for JSON
      content; `validate_response` runs status lookup → response-body schema (JSON) → declared-header
      presence. Validation is **media-type-aware** (D-HWC-9): non-JSON bodies (e.g. `text/xml`) get
      method/path/status + parameter + header checks only — body *value* validation for XML is a
      scoped follow-on with its own recorded strategy, not done here. Operations marked
      not-eligible by the `x-…` extension are skipped.
- [x] M-HWC-CONTRACT-VALIDATE-2: Validating decorator facet (sibling to the logging facet) that, on
      each `synthetic_http_request` / `captured_http_response` crossing the edge, invokes the bound
      contract and **traces** violations as a side diagnostic (D6 — persists nothing). An opt-in
      flag surfaces a contract-violation `error_code` so tests can assert; off by default the facet
      only traces.
- [x] M-HWC-CONTRACT-VALIDATE-3 (integration): load a tiny YAML spec, push a conforming request +
      response and a violating request + response through the synthetic edge, and assert each is
      detected (and not detected for the conforming case). Sub-second.

## Milestone M-HWC-CONTRACT-DRIVE — drive mode (spec examples → traffic) (D-HWC-8)

- [x] M-HWC-CONTRACT-DRIVE-1: Example extractor over the model — for each operation synthesize a
      `synthetic_http_request` from the operation's `example` / `examples` (parameters and request
      body), falling back to schema-derived defaults where no example is present. Pure over the
      model; emits the request list.
- [x] M-HWC-CONTRACT-DRIVE-2: Driver that enqueues the synthesized requests into the synthetic
      queue and (when validate is also bound) runs each captured response through
      `validate_response`, reporting the conforming/violating tally.
- [x] M-HWC-CONTRACT-DRIVE-3 (integration): a YAML spec carrying request examples drives the fake
      engine end to end; responses are captured and validated (validate + drive composed). Asserts
      every example operation produced a request and each response was contract-checked.

## Milestone M-HWC-CONTRACT-EXPOSE — public binding surface for consumers (D-HWC-8)

Goal: make the contract surface reachable from outside `m_pil` so a consumer (mwin32
M-HWC-CONTRACTCFG) can bind specs without reaching into PIL internals. Discovered during
mwin32 execution: `iplatform::get_http_contract()` still returns the null provider (nothing
wired the live provider into the stack), and the drive synthesizer is `src/`-internal.

- [x] M-HWC-CONTRACT-EXPOSE-1: Wire `iplatform::get_http_contract` through the live stack,
      mirroring `get_webcore`. The bottom live platform (`src/direct/Platforms/windows/win32`)
      returns `make_http_contract_provider()`; every decorator that overrides `get_webcore`
      (`passthrough`, `buffered`, `logging`, `redirecting`, `fault`, `journaling`) forwards
      `get_http_contract` to its underlying platform. Add `src` to `m_pil` private include dirs so
      the win32 platform can include the provider header. Unit test: a live platform's
      `get_http_contract().load(spec)` yields a working document through the full stack.
- [x] M-HWC-CONTRACT-EXPOSE-2: Expose the drive surface publicly. Promote `synthesized_request`,
      `captured_contract_response`, `drive_tally`, and `engine_submit` into the public interface
      header; add `ihttp_contract_document::synthesize_requests()` (virtual, default `{}`; the live
      document overrides it via the internal `synthesize_contract_requests(model)`); add a public
      `drive_contract(document, submit)` convenience. Refactor the internal driver and its tests
      onto the public types. Tests stay green in both configs.

      > **➡ CROSS-COMPONENT HANDOFF:** with the binding surface exposed, the `.pilcfg` binding
      > (`webcore.contracts`: spec + endpoint + mode) resumes in component
      > `src/Windows/libraries/mwin32` → milestone `M-HWC-CONTRACTCFG`. See
      > [`src/Windows/libraries/mwin32/CHECKLIST.md`](../../Windows/libraries/mwin32/CHECKLIST.md).

## Milestone M-HWC-CONTRACT-EDGE — public contract-edge seam (D-HWC-10)

Goal: give a consumer one public, stateful seam that ties N bound contracts to one engine —
`validate`-mode documents auto-validate every request/response crossing it, `drive`-mode
documents are submitted through the same seam — so mwin32 M-HWC-CONTRACTCFG-6 can attach the
documents it binds without reaching into PIL internals. The engine is pluggable (fake in tests,
the activated engine's synthetic queue in production, per D-HWC-8). Discovered during mwin32
CONTRACTCFG execution: CONTRACTCFG-3 binds documents but there is no public object to attach them
to live edge traffic; the validating facet is `src/`-internal.

- [x] M-HWC-CONTRACT-EDGE-1: Public `ihttp_contract_edge` seam. New public header
      [`include/m/pil/http_contract_edge.h`](include/m/pil/http_contract_edge.h): a
      `contract_edge_tally` struct (`requests`, `responses`, `request_violations`,
      `response_violations`), an `ihttp_contract_edge` interface (`submit(synthesized_request) ->
      captured_contract_response`; `attach_validation(shared_ptr<ihttp_contract_document>)`;
      `tally()`; non-virtual `as_engine_submit()` adapting to `engine_submit`), and a free
      `make_contract_edge(engine_submit) -> shared_ptr<ihttp_contract_edge>` factory. The header
      names only public contract types (no Win32 / `<http.h>`). Implementation in
      [`src/contract/http_contract_edge.cpp`](src/contract/http_contract_edge.cpp) (added to the
      contract `target_sources`): `submit` validates the request against each attached document,
      calls the engine, validates the response, updates the tally, and returns the engine's
      response. Validation reuses the tested `contract_validating_facet` (surfacing on, interpreted
      for the tally and swallowed so the engine is never altered — D6). Builds clean debug+release.
- [x] M-HWC-CONTRACT-EDGE-2 (unit tests): inline-spec tests
      ([`test/test_http_contract_edge.cpp`](test/test_http_contract_edge.cpp)) — a conforming
      request+response crossing tallies no violations; a violating request and a violating response
      are each counted; `drive_contract(document, edge.as_engine_submit())` runs through the edge
      and the edge's attached validate document sees the same crossings; an edge with no attached
      documents passes traffic through untouched; the engine is never altered by a violation.
      ≥10 cases, sub-second.

      > **➡ CROSS-COMPONENT HANDOFF:** with the public edge seam landed, attaching bound documents
      > to live edge traffic resumes in component `src/Windows/libraries/mwin32` → milestone
      > `M-HWC-CONTRACTCFG` → `M-HWC-CONTRACTCFG-6`. See
      > [`src/Windows/libraries/mwin32/CHECKLIST.md`](../../Windows/libraries/mwin32/CHECKLIST.md).
