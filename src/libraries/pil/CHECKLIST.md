# pil CHECKLIST

The **Hostable Web Core (HWC) isolation** surface — the third PIL surface (after registry and
filesystem), surfaced through the `mwin32` Win32 shim — is **complete** (Phases 1–4,
D-HWC-1…D-HWC-11) and has been moved to
[COMPLETED-CHECKLIST.md](COMPLETED-CHECKLIST.md) (see "Moved 2026-06-19 — HWC isolation surface").
Registry (first surface) is also in [COMPLETED-CHECKLIST.md](COMPLETED-CHECKLIST.md).

Remaining active work: the **filesystem surface** (the second surface), tracked by the
**M-FS-STREAMS** milestone below. Its tier-1 (redirection-backed file content, D16/D17) is
complete; only the tier-2 alternate-data-stream sub-namespace (**M-FS-STREAMS-2**) stays
**deferred**. The two completed filesystem sibling milestones (M-FS-MONITOR-REDIR,
M-FS-SHORTNAME) are retained below as nearby context until the filesystem surface is fully
closed.

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
