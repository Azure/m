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
