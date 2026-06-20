# pil completed checklist

Append-only record of completed checklist groups moved out of
[CHECKLIST.md](CHECKLIST.md). Design rationale lives in
[DESIGN-NOTES.md](DESIGN-NOTES.md); implementation notes are in its
"Implementation notes" section.

## Moved 2026-06-15 — registry surface decorators (D1–D8)

The first isolation surface (registry): buffered sealed whole-key snapshot, logging
off persistence + floating tap, journaling ordered replay, buffered `delete_tree` /
multi-component `create_key`, fault injection, and the legacy save-path cleanup.

### Milestone M-PS — buffered persisted state: sealed whole-key snapshot (D2–D5)

- [x] M-PS-1: Audit current capture vs. serialization. Confirm what the in-memory
      buffered mirror holds today for an *observed-but-unmodified* key (whole key vs.
      structure-only) and what `registry::save_xml` / `key::save_xml`
      (`src/buffered/registry.cpp`, `src/buffered/registry_key_key_operations.cpp`)
      actually emit. Record findings as the concrete delta needed for M-PS-2/3. No code
      change; this scopes the rest of the milestone. (D2)
- [x] M-PS-2: Whole-key capture on touch (D3, D4). Ensure that touching a key materializes
      it into the mirror as a whole key: metadata (incl. `last_write_time`), all values
      (whole, in memory), and the child subkey-name list. Implement the best-effort
      capture with `last_write_time` bracketing and bounded retry on torn reads. Tests
      (deterministic, real registry): eager whole-value capture (a buffered key serves the
      value captured at open time after the underlying is mutated) and metadata capture
      (`last_write_time` matches the underlying). NOTE: deterministic tests for the
      concurrent torn-read→retry and vanished-mid-capture→drop paths require a controllable
      mock `ikey` injection harness; deferred to M-PS-MOCK below rather than blocking this
      item.
- [x] M-PS-MOCK (test infrastructure, can follow M-PS): Build a minimal controllable mock
      `ikey`/underlying so the best-effort capture edge cases can be tested
      deterministically: torn read (changing `last_write_time` across the capture bracket)
      → bounded retry, and value vanished between enumeration and load → dropped from the
      captured set. Reusable for later milestones (journaling, fault injection).
- [x] M-PS-3: Whole-mirror serialization (D2, D3). Change `key::save_xml` /
      `registry::save_xml` to emit observed-whole keys (metadata + values + subkey names)
      in addition to writes and tombstones — no negative space. Keep the `<Platform>` /
      `<Registry>` schema surface-neutral. Unit tests asserting an observed-but-unmodified
      key round-trips into the artifact.
- [x] M-PS-4: Sealed load (D2). `create_platform_from_persisted_xml` rebuilds a sealed
      world from the whole-mirror artifact (no fall-through to a real platform). Restore
      metadata + values + subkey-name lists. Unit tests: a key observed in run #1 is
      readable from the loaded snapshot with no underlying platform.
- [x] M-PS-5: Lazy consistency repair (D5). On load, stamp `T_load`. On the read that
      exposes a contradiction (enumeration lists subkey `X` but opening `X` fails), drop
      `X` from the enumeration and set that key's `last_write_time = T_load`; never
      re-query a real platform. Unit tests for the repair-and-restamp invariant
      (re-enumeration after repair is consistent; stamp advanced).
- [x] M-PS-6: Integration test. End-to-end: build a buffered platform over a live
      registry, touch/modify a representative set of keys, save, reload as a sealed
      snapshot, and assert the loaded world reproduces what run #1 observed and is
      self-consistent under repeated reads/enumerations.

### Milestone M-LOG-OUT — remove logging from persisted artifacts (D6)

- [x] M-LOG-OUT-1: Stop the logging layer emitting `<Log>` into the saved `<Platform>`.
      `logging::platform::save` (`src/logging/platform.cpp`) currently appends a `<Log>`
      child; remove that so persisted state never carries the log. Pass-through save only.
- [x] M-LOG-OUT-2: Route logging output to a side diagnostic artifact (not any persistence
      form). Define where the requested-vs-done trace goes when needed. Tests asserting a
      saved `<Platform>` contains no `<Log>` and that the side trace is still obtainable.

### Milestone M-LOG-FLOAT — injectable logging tap (D6)

- [x] M-LOG-FLOAT-1: Make the logging decorator insertable between any two layers rather
      than only outermost. Define the wrapping API and the requested-vs-done record shape.
- [x] M-LOG-FLOAT-2: Tests placing the logging tap at multiple depths and asserting it
      captures the requested and done operations at that depth without altering behavior.

### Milestone M-JOURNAL — ordered replay capability (D7, deferred)

- [x] M-JOURNAL-1: Define the journaling artifact (ordered verb stream) and the journaling
      decorator's write/read append behavior, distinct from the buffered snapshot.
- [x] M-JOURNAL-2: Implement load-side ordered replay of the journal onto a base world.
- [x] M-JOURNAL-3: Tests: record a sequence, replay it, assert observable equivalence.

### Milestone M-BUFTREE — buffered delete_tree (gap surfaced by M-JOURNAL-3)

- [x] M-BUFTREE-1: Implement `buffered::key::delete_tree`
      (`src/buffered/registry_key_key_operations.cpp`, currently throws
      `not_implemented`). Recursively tombstone the named subkey and all of its
      descendants in the overlay (handling mirrored-but-unmaterialized nodes),
      matching the create-or-open / tombstone semantics already used by
      `create_key` / `delete_key`. Then drive the DeleteTree verb through the
      journaling record/replay test against a buffered base world (extend
      `test_journaling.cpp`).

### Milestone M-BUFCREATE — buffered multi-component create_key (gap surfaced by mwin32 M-ALIAS-4)

- [x] M-BUFCREATE-1: `buffered::key::create_key`
      (`src/buffered/registry_key_key_operations.cpp`) rejects any `key_name` with
      a parent path (`has_parent_path()` → `invalid_parameter`), so it creates only
      one level at a time. Live `RegCreateKeyExW` instead auto-creates every
      intermediate key in a multi-component path. The mwin32 shim forwards the full
      Win32 subkey path straight to `create_key`, so a buffered client doing
      `RegCreateKeyExW(HKCU, L"A\\B\\C", ...)` throws instead of succeeding (worked
      around in `test_mwin32_alias.cpp` by using a single-component subkey). Make
      `create_key` walk a multi-component `key_name`, creating/opening each
      intermediate level in the overlay (matching the create-or-open semantics used
      elsewhere), and return the leaf. Tests: multi-level create materializes all
      intermediates; re-creating an existing path is idempotent; the existing
      single-component behavior is unchanged.

### Milestone M-FAULT — fault-injecting layer (D8)

- [x] M-FAULT-1: Define the fault-script artifact and grammar: rule =
      (operation type, path/pattern, Nth-occurrence counter) → action (status / error
      code). Separate input file, not part of `<Platform>`.
- [x] M-FAULT-2: Implement the fault-injecting decorator with per-rule counted matching
      (stateful), e.g. "third open of X fails with out-of-resources".
- [x] M-FAULT-3: Tests: counted matching fires on the Nth occurrence and not before;
      multiple rules compose; non-matching operations pass through unchanged.

### Milestone M-CLEANUP — legacy save path (do LAST)

- [x] PERSIST-1 (DEFERRED DECISION — do LAST): Decide the fate of the legacy
      concrete buffered save path. The buffered layer carries a second,
      file-based save API that is parallel to (and not reachable from) the
      public node-based `iplatform::save`:
        - `buffered::platform::save(persistence_format, std::filesystem::path)`
          (`src/buffered/platform.cpp`, decl in `src/buffered/buffered.h`)
        - private `buffered::platform::save_xml(m::locked_t, std::filesystem::path)`
          (`src/buffered/platform.cpp`)
        - `enum class persistence_format { xml }` (`src/buffered/buffered.h`)
      It is currently dead code AND latently buggy: `save_xml(locked_t, path)`
      calls `doc.document_element()` on an EMPTY document (null node) and then
      `set_name`/append onto it. The working persistence path is the polymorphic
      `save(save_flags, save_contents, pugi::xml_node&)` virtual + the public
      `m::pil::platform::save(path, ...)` wrapper (`src/platform.cpp`).
      USER DIRECTIVE (2026-06-14): leave this legacy code in place for now in
      case we can salvage something from it; only delete it at the very end if
      nothing ends up needing it. When this item is reached: if still unused,
      remove the three artifacts above (and their `persistence_format` enum);
      otherwise fold whatever is worth keeping into the node-based path and
      record the rationale in a DESIGN-NOTES entry.


## Moved 2026-06-15 — filesystem path foundation (D10, D11, D12)

### Milestone M-FS-PATH — `file_path`, roots, canonicalization, ordinal sort keys (D10, D11, D12)

The path type is the foundation everything else builds on, so it lands first and fully
tested, independent of any provider.

- [x] M-FS-PATH-1: Define `file_root` and `file_path` in `include/m/pil/file_path.h`,
      mirroring the shape of `key_path.h`. `file_root` is a *kind discriminant + root
      text* (Windows: drive `C:`, UNC `\\server\share`, device `\\.\…`, extended-length
      `\\?\…` and `\\?\UNC\server\share\…`; POSIX: `/`; plus rootless ⇒ relative), **not**
      a closed enum (D10). `file_path` = optional `file_root` + normalized relative
      segments; absence of a root ⇒ relative. Parse each root family from a string. Unit
      tests: every root family round-trips; relative vs absolute classification.
- [x] M-FS-PATH-2: Canonicalization (D11). Normalize `/`↔`\` on Windows (POSIX `/` only);
      collapse repeated separators; strip trailing separator except a bare root; resolve
      `.`/`..` lexically. **Suppress all of this inside `\\?\` / `\\?\UNC\` paths** — only
      the prefix is recognized, the remainder is preserved verbatim (Win32 does not
      normalize extended-length paths; `\\?\C:\a\..\b` is a literally different object than
      `C:\a\..\b`). Provide `parent_path`, `split_parent_path_and_leaf_name`,
      `relative_path`, `operator/`. Unit tests including the `\\?\` non-normalization cases
      and `..` underflow past the root.
- [x] M-FS-PATH-3: Ordinal case handling (D12). Select the name comparator / sort key by
      surface: Windows ordinal case-insensitive (reuse `m::case_insensitive_less`), POSIX
      ordinal case-sensitive. Stored case is always preserved (never folded). Optionally
      precompute a norm_ignorecase sort key per segment so lookups don't re-fold on every
      compare. Unit tests: `Foo` == `foo` (Windows) / `!=` (POSIX); original case survives
      a parse→string round-trip; equality/ordering match the comparator.
- [x] M-FS-PATH-4: Edge-case sweep + integration. ≥10 normal cases plus edges: mixed
      separators, UNC vs drive, `\\?\` literal vs normalized sibling, empty/relative,
      `..` past root, deeply nested, trailing dot/space note. Integration-style test
      cross-checking `file_path` canonicalization against a table of expected results.

## Moved 2026-06-15 — filesystem interfaces, base types, and platform wiring (D9, D13)

### Milestone M-FS-IFACE — interfaces, base types, and platform wiring (D9, D13)

Defines the surface contract and wires it into `iplatform`, but with no live provider yet
(a null/throwing provider keeps it compiling cross-platform).

- [x] M-FS-IFACE-1: `include/m/pil/filesystem_base_types.h` — node-kind enum
      (`directory` / `file`), a metadata struct (size, create/modify/access timestamps,
      attribute flags), a directory-entry struct (name + node-kind + metadata) reflecting
      the **unified namespace** (D13), and an access-mode analogue of `sam`.
- [x] M-FS-IFACE-2: `include/m/pil/filesystem_interfaces.h` — `ifilesystem`
      (`open_root(file_root) → idirectory`, the analogue of `open_predefined_key`),
      `idirectory` (create/open directory, create/open file, remove a child by name,
      `delete_tree`, rename/move, enumerate entries, `query_information`), and `ifile`
      (`query_information`; content deferred per D14). Follow the registry disposition
      pattern exactly: ec-form primitives + throwing wrappers + `tolerate_not_found`
      tentative opens.
- [x] M-FS-IFACE-3: Add `iplatform::get_filesystem()` mirroring `get_registry()`
      (`platform_interfaces.h`), and keep the persisted `<Platform>` surface-neutral with
      room for a `<Filesystem>` child beside `<Registry>`. Base wiring returns a
      null/not-implemented filesystem until M-FS-DIRECT. Unit test: `get_filesystem()`
      resolves through the stack.
- [x] M-FS-IFACE-4: Convenience value-wrapper layer `include/m/pil/filesystem.h`
      (`filesystem_class`, `directory`, `file`) mirroring `registry.h`
      (`registry_class`, `key`); wire `platform::get_filesystem()` in `platform.h`.
      Unit tests over the wrappers against the null provider (shape/compile-level).

## Moved 2026-06-15 — live Windows filesystem provider + Linux stub (D9, D13)

The direct filesystem provider backing `ifilesystem` / `idirectory` / `ifile` against the
live Windows filesystem, mirroring the direct registry provider: long-path-aware root open,
unified-namespace enumeration, metadata stat, namespace mutations with Win32→`m::` error
mapping, the Linux no-op stub (covered by the existing `create_platform` `#else` branch),
and an end-to-end integration test driving a temp tree against `std::filesystem` ground
truth.

### Milestone M-FS-DIRECT — live Windows provider + Linux stub (D9, D13)

- [x] M-FS-DIRECT-1: `src/direct/Platforms/windows` filesystem provider — root open
      (drive / UNC / `\\?\`), directory open + enumerate (`FindFirstFileExW` /
      `FindNextFileW`), `query_information` (`GetFileInformationByHandleEx`), file open for
      metadata. Long paths via the `\\?\` prefix. Reads only.
- [x] M-FS-DIRECT-2: Namespace mutations — `create_directory` (`CreateDirectoryW`),
      `create_file` (`CreateFileW`), remove (`DeleteFileW` / `RemoveDirectoryW`),
      `delete_tree`, rename/move (`MoveFileExW`). Map Win32 errors to the established
      `m::` exception categories (`not_found`, `already_exists`, `access_denied`,
      `sharing_violation`, …) used by the registry provider.
- [x] M-FS-DIRECT-3: Linux direct stub under `src/direct/Platforms/Linux`, mirroring the
      registry Linux stub (`M_NOT_IMPLEMENTED`), so the filesystem surface compiles on both
      platforms while only Windows is functional.
- [x] M-FS-DIRECT-4: Integration test (Windows). Drive a temp directory tree through the
      direct provider — create nested directories/files, enumerate (unified namespace),
      stat, rename/move, delete, `delete_tree` — and assert observations against
      `std::filesystem` ground truth.

## Moved 2026-06-15 — pass-through filesystem facet (D9)

The transparent pass-through filesystem facet in `src/passthrough`, mirroring the registry
pass-through facet: `filesystem` / `directory` / `file` wrappers each hold a shared_ptr to
the underlying interface and forward every verb unchanged, re-wrapping returned node
interfaces in their pass-through counterparts; `platform::get_filesystem` is wired to return
the cached wrapper built over `m_underlying_platform->get_filesystem()`. Observable
equivalence to the direct provider is verified by a Windows integration test that drives the
same operations through a pass-through layer over a live direct platform and asserts identical
observations against `std::filesystem` ground truth.

### Milestone M-FS-PASS — pass-through filesystem facet (D9)

- [x] M-FS-PASS-1: Add the filesystem facet to `src/passthrough` (platform / filesystem /
      directory / file wrappers) forwarding every op unchanged; wire `get_filesystem`
      through. Tests: observable equivalence to the direct provider.

## Moved 2026-06-16 — M-FS-BUF buffered filesystem overlay (sealed whole-node snapshot)

Reinterprets the registry buffered decisions for the unified filesystem namespace.
Content is **not** captured (D14) — the snapshot is namespace + metadata.

- [x] M-FS-BUF-1: Overlay node model. Directory node = child-entry map keyed by
      norm_ignorecase sort key with original-case preserved (D12), holding `(name, kind)`
      entries; file node = metadata only (no bytes, D14). Tombstones and
      mirrored-but-unmaterialized placeholders as in the registry overlay. Whole-node
      capture on touch with `last_write_time` bracketing + bounded retry on torn reads
      (D3, D4 analogues, non-recursive).
- [x] M-FS-BUF-2: Namespace mutations in the overlay — create directory/file, remove,
      rename/move (a unified-namespace move re-keys the entry), `delete_tree` as a single
      tombstone (the M-BUFTREE technique), multi-segment create walk (the M-BUFCREATE
      technique). Create-or-open semantics throughout. Unit tests for each verb against the
      overlay.
- [x] M-FS-BUF-3: Whole-mirror serialization + sealed load. Emit observed-whole nodes
      (metadata + child entry name/kind lists; no negative space; tombstones) into a
      `<Filesystem>` child of the surface-neutral `<Platform>` (D2, D3). Sealed load rebuilds
      the world with no fall-through, with lazy consistency repair + restamp on contradiction
      (D5). Unit tests: an observed-but-unmodified node round-trips; a sealed snapshot serves
      the namespace with no underlying provider.
- [x] M-FS-BUF-4: Controllable mock `idirectory` / `ifile` (the M-PS-MOCK pattern) to make
      the best-effort capture edge cases deterministic: torn read (changing
      `last_write_time` across the bracket) → bounded retry; entry vanished between
      enumeration and load → dropped. Reusable by later FS milestones.
- [x] M-FS-BUF-5: Integration test. Build a buffered filesystem over a live temp tree,
      touch/modify the namespace, save, **delete the live tree**, reload as a sealed
      snapshot, and assert the namespace + metadata are reproduced and self-consistent
      under repeated reads/enumerations. Explicitly assert the D14 limitation (file *content*
      reads are out of scope for the sealed snapshot) so the boundary is test-documented.

## Moved 2026-06-16 — M-FS-REDIR redirecting filesystem facet (D9 / D12)

- [x] M-FS-REDIR-1: Add the filesystem facet to `src/redirecting` — path-prefix redirection
      using the ordinal ci match (D12), mirroring the registry redirector. Tests: a
      redirected prefix sends operations to the target subtree; non-matching paths pass
      through; original-case preserved.


## Moved 2026-06-16 — M-FS-LOG logging tap for filesystem (D6)

- [x] M-FS-LOG-1: Add the filesystem facet to `src/logging` — record `Filesystem.*`
      mutation entries (CreateDirectory, CreateFile, Remove, DeleteTree, Rename) with the
      requested-vs-done shape into the floating diagnostic `<Log>`; reads pass through;
      never written into `<Platform>` (D6). Tests place the tap at varied depths and assert
      capture without altering behavior.

## Moved 2026-06-16 — M-FS-JOURNAL ordered replay of filesystem namespace verbs (D7)

- [x] M-FS-JOURNAL-1: Add the filesystem facet to `src/journaling` — record the FS
      namespace verbs into the ordered `<Journal>` artifact (mutations only, per D14 no
      stream content), and extend replay to reissue them onto a base world. Tests: record a
      namespace sequence (including a `delete_tree` and a rename/move), replay onto a fresh
      base, assert observable namespace equivalence.

## Moved 2026-06-16 — M-FS-FAULT fault grammar extension for filesystem (D8)

## Milestone M-FS-FAULT — fault grammar extension for filesystem (D8)

- [x] M-FS-FAULT-1: Extend the fault vocabulary with filesystem operations
      (create_directory, create_file, open_directory, open_file, remove, delete_tree,
      rename) targeting a `file_path`, with the same per-rule counted matching, and add the
      filesystem facet to `src/fault`. Extend the public `include/m/pil/fault.h` façade.
      Tests: counted Nth-occurrence firing, multi-rule composition, non-matching
      pass-through — over a sealed buffered filesystem snapshot.

## Moved 2026-06-16 — M-FS-MONITOR filesystem change monitor (D9, D15)

## Milestone M-FS-MONITOR — filesystem change monitor (D9)

- [x] M-FS-MONITOR-1: Filesystem change-notification surface mirroring
      `iregistry_monitor` (`ReadDirectoryChangesW` on Windows; passthrough / buffered /
      logging facets as the registry monitor has). Tests for create / rename / delete
      notifications. May be scheduled after M-FS-FAULT if change notification is not yet
      needed by a consumer.

## Moved 2026-06-19 - HTTP contract recorder (derive OpenAPI YAML from observed traffic)

Feature: PIL side of the wire-capture lifecycle demo - observe clean request/response crossings and emit a loadable OpenAPI 3.0 YAML spec (the `derive the contracts` capability), the inverse of `ihttp_contract::load`.

### Milestone M-REC - recorder + OpenAPI YAML emitter

- [x] REC-1: OpenAPI model -> YAML emitter (`emit_openapi_yaml`); round-trip load->emit->reload structural-equality test.
- [x] REC-2: Body-shape inference (`infer_json_schema` / `merge_json_schema`); object/array/scalar/empty + required-key intersection tests.
- [x] REC-3: Internal `http_contract_recorder` accumulation (observe_request/observe_response, correlate by method+path, merge schemas + statuses, emit_spec); multi-op/multi-status/idempotent tests.
- [x] REC-4: Public facade `make_http_contract_recorder` + close-the-loop test (derive spec, load through `make_http_contract_provider`, validate accepts clean / rejects mutated request+response). CROSS-COMPONENT HANDOFF to mwin32 M-WIRECAP-CFG WC-5.

## Moved 2026-06-19 — HWC isolation surface (third PIL surface): Phases 1–4 complete (D-HWC-1…D-HWC-11)

Hostable Web Core (HWC) isolation — the `iwebcore` engine surface surfaced through the `mwin32`
`mWebCore*` shim. Phase 1 (surface + live provider + decorator facets), Phase 2 (config/registry
isolation), Phase 3 (network edge), Phase 4 (OpenAPI/Swagger contract binding) all delivered.

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

## Milestone M-HWC-ENGINE-EDGE — activatable synthetic HTTP engine + public submit/observe seam (D-HWC-11)

Goal: make an `iwebcore` engine *activatable and drivable in CI* — without IIS / a real
`hwebcore.dll` — and give a consumer a public seam to (a) submit a request into the activated
engine's in-process synthetic HTTP edge and get the captured response back, and (b) observe every
request/response **crossing** that edge (drive-injected *and* autonomous client traffic). This is
the missing prerequisite for mwin32 `M-HWC-CONTRACTCFG-7`, whose stated goal — "requests/responses
crossing the synthetic edge are auto-validated, and drive mode executes against the running
engine" — needs both a live crossing tap and a real activated engine to validate against.

- [x] M-HWC-ENGINE-EDGE-1: Record decision **D-HWC-11** in [`DESIGN-NOTES.md`](DESIGN-NOTES.md)
      (Tier 1 index + detail; no Tier 2 in PIL): a public, cross-platform synthetic-HTTP edge seam
      with two realizations (the intercepting webcore over a real engine; an in-process engine for
      CI), plus the rule that validate-mode *live tapping* is a per-crossing observer (D6: a side
      diagnostic that never alters the engine). State the acknowledged limit that real `hwebcore`
      activation (IIS) stays outside CI and is exercised only by sharing the identical bridge code.
      Then add the public header [`include/m/pil/synthetic_http_edge.h`](include/m/pil/synthetic_http_edge.h)
      (namespace `m::pil`): `using crossing_observer = std::function<void(synthesized_request const&,
      captured_contract_response const&)>;`, a `struct isynthetic_http_edge` with pure virtual
      `captured_contract_response submit(synthesized_request const&, std::chrono::milliseconds
      timeout)` and `void add_crossing_observer(crossing_observer)`, and a free
      `engine_submit make_engine_submit(isynthetic_http_edge&, std::chrono::milliseconds timeout)`
      adapter. Add an `iwebcore_instance::synthetic_http_edge()` accessor (virtual, default
      `nullptr`) to [`include/m/pil/webcore_interfaces.h`](include/m/pil/webcore_interfaces.h) using
      only a forward declaration of `isynthetic_http_edge` (no coupling of the webcore header to the
      contract types). `null_webcore_instance` keeps the default `nullptr`. The header names only
      public contract types — no Win32 / `<http.h>`. Builds clean debug+release.
- [x] M-HWC-ENGINE-EDGE-2: Give the internal `synthetic_http_queue`
      ([`src/intercepting/intercepting_webcore.h/.cpp`](src/intercepting/intercepting_webcore.cpp))
      an optional crossing-observer hook invoked when a response completes
      (`complete_response`), delivering the originating `synthetic_http_request` paired with its
      `captured_http_response`. The queue must retain enough of each dequeued request (keyed by
      `HTTP_REQUEST_ID`) to hand the observer the request that produced the response. Observers are
      invoked outside the queue lock. Keep the existing direct-API tests green and add a unit test
      that a serviced request→response pair reaches a registered observer.
- [x] M-HWC-ENGINE-EDGE-3: Implement the intercepting `webcore_instance`'s
      `synthetic_http_edge()` over its `synthetic_queue` (the real-engine path). Activation with
      synthetic mode enabled creates the queue; the instance returns an adapter that translates the
      public `synthesized_request` ↔ internal `synthetic_http_request` and `captured_http_response`
      ↔ `captured_contract_response`, implements `submit` as enqueue + `wait_for_response(timeout)`,
      and forwards `add_crossing_observer` to the queue hook from EDGE-2. Unit test by servicing the
      queue directly (the test plays the engine: dequeue, build a response, `capture_response` /
      `complete_response`) and asserting `submit` returns it and observers fire.
- [x] M-HWC-ENGINE-EDGE-4: Add an **in-process engine** provider
      `std::shared_ptr<iwebcore> make_in_process_webcore(synthetic_request_handler handler)` (new
      `using synthetic_request_handler = std::function<captured_contract_response(synthesized_request
      const&)>;`) — an `iwebcore` whose `activate` enables synthetic mode, owns a
      `synthetic_http_queue`, and runs a worker thread that loops `dequeue_request` → `handler` →
      `capture_response` / `complete_response`; the returned instance exposes `synthetic_http_edge()`
      over that queue and joins the worker on destruction. This is the deterministic, IIS-free
      engine the synthetic edge (D-HWC-6 Tier B) was designed for. Unit tests: activate, build an
      `engine_submit` via `make_engine_submit`, submit conforming + violating requests and assert
      the handler's responses come back; a registered crossing observer sees every serviced
      crossing; a `null_webcore_instance` yields no edge (`synthetic_http_edge() == nullptr`, and
      `make_engine_submit` over a null edge is a null `engine_submit`); clean shutdown with
      in-flight requests. ≥10 cases, sub-second.
- [x] M-HWC-ENGINE-EDGE-5 (integration): over an activated `make_in_process_webcore`, compose both
      modes against one engine: `drive_contract(document, make_engine_submit(edge, timeout))` for a
      drive document, and `edge.add_crossing_observer(...)` running a validate document's
      `validate_request` / `validate_response` for the live tap. Drive a YAML spec end to end and
      assert every example operation produced a request, a deliberately non-conforming response is
      reported as a violation, and the validate observer saw the same crossings. Sub-second.
