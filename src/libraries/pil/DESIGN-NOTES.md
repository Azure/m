# PIL design notes

Decisions about the Platform Isolation Layer (PIL). Tier 1: current canonical
decisions. Each decision has a stable D-number referenced from CHECKLIST.md items.

## Decision index

| ID | Title |
|---|---|
| D1 | PIL is a stack of policy-intent decorators over the real platform |
| D2 | Buffered persisted state is a sealed whole-key snapshot (no negative space) |
| D3 | Captured key = metadata + values + subkey names, non-recursive |
| D4 | Best-effort capture; `last_write_time` is the version stamp |
| D5 | Load-time consistency repair: lazy repair-and-restamp on contradiction |
| D6 | Logging is a side diagnostic, never part of any persisted artifact |
| D7 | Journaling owns ordered replay (separate artifact, deferred capability) |
| D8 | Fault injection is a counted-rule script consumed by a fault-injecting layer |
| D9 | Filesystem is the second isolation surface; same decorator stack, surface-neutral `<Platform>` |
| D10 | Filesystem path & root model (`file_path`): roots are open-ended, not a closed enum |
| D11 | Filesystem path canonicalization: separators, `\\?\` normalization suppression, dot segments |
| D12 | Case-insensitivity via ordinal sort keys, never by folding stored case |
| D13 | Unified entry namespace: a filesystem node is a directory xor a file |
| D14 | Stream content & alternate-stream sub-namespace are deferred (acknowledged incorrectness) |
| D15 | Filesystem change monitor mirrors the registry monitor (detailed notifications) |
| D16 | Deferred file content is redirection-backed (namespace-level only), not byte-captured |
| D17 | Captured entries carry the host's alternate (8.3 short) name as a lookup alias |
| D-HWC-1 | Hostable Web Core is an *engine* surface, composed from the state surfaces it reads |
| D-HWC-2 | `iwebcore` on `iplatform` with a default null provider (mirrors `get_filesystem`) |
| D-HWC-3 | Engine bound via `LoadLibraryExW` from the absolute `system32\inetsrv` path (+ `inetsrv` on the dependency search); the three proc addresses are the test seam |
| D-HWC-4 | Isolate the engine's config/registry reads by materialization (default) **or** module-scoped Detours interception (opt-in) |
| D-HWC-5 | Single activation per process, enforced on the session |
| D-HWC-6 | Network edge is the deferred `ihttp_listener` surface via namespace redirection (private addresses/ports) |
| D-HWC-7 | Detours is an opt-in, module-scoped, off-by-default complementary envelope bounded to the HWC surface |

---

## D1 — PIL is a stack of policy-intent decorators

The purpose of PIL is to disconnect product code from the real platform so we can
exercise scenarios against it: record behavior, replay from a known starting point
without affecting persistent platform state, and inject faults. Each capability is a
distinct decorator named by its **policy intent**, layered over the real platform:

| Layer | Write behavior | Read behavior | Persists |
|---|---|---|---|
| pass-through | forward | forward | nothing |
| buffered | land in in-memory overlay; mirror touched keys whole | overlay → underlying | sealed state snapshot |
| journaling | forward + append verb | forward + append verb | ordered replayable journal |
| logging | forward + append trace | forward + append trace | nothing (side diagnostic) |
| fault-injecting | consult fault script | consult fault script | nothing (consumes a script) |

The major providers (buffered, pass-through, fault-injecting) have fixed connection
semantics. Logging is the exception — see D6.

Surfaces: registry is implemented first; filesystem is the second surface (planned in
detail — see D9–D14 and the M-FS-* milestones), then others (e.g. network). The
`<Platform>` schema is surface-neutral (holds `<Registry>` today, room for `<Filesystem>`
etc. later); only registry is implemented so far.

## D2 — Buffered persisted state is a sealed whole-key snapshot

The persisted artifact for the buffered registry is a **sealed state snapshot**: a
loaded buffered platform must be a self-contained world that does not fall through to a
real platform. Every **touched** key is captured **whole**; deletes are tombstones.

**No negative space.** We do not record "observed-absent". Registry contents are too
volatile run-to-run to make absence meaningful; isolation does not depend on it.

This is a change from the M4 serializer, which emitted **modifications + tombstones
only** (observed-but-unmodified keys were dropped and reconstructed by falling through to
a real platform on load). The persistence work promotes `save_xml` from
*modifications-only* to *whole-mirror*: emit observed-whole keys as well, so the loaded
world is sealed.

## D3 — Captured key = metadata + values + subkey names (non-recursive)

A captured key carries exactly three facets:
1. metadata (including `last_write_time`),
2. values (name / type / data, whole — including large values held in memory),
3. the list of child **subkey names** (enumeration result).

It does **not** recurse into children's contents. A child name in the list is a
*separate* capturable key that may or may not itself be present.

## D4 — Best-effort capture; `last_write_time` is the version stamp

Capture is best-effort; this is not a transactional registry. Algorithm when capturing a
touched key:
1. query key info → `last_write_time` T0,
2. enumerate values (whole) + child subkey names,
3. query key info again → T1,
4. if the read is obviously torn, re-enumerate (bounded retries; small limit),
   otherwise accept it.

`last_write_time` is treated as the key's **version stamp**: a captured key's contents
are immutable for a given `last_write_time`. Any change to a key's captured contents must
advance its `last_write_time`. A modification date that changed while contents did not is
not a concern and need not be flagged. Vanished subkeys/values mid-enumeration are simply
absent from the captured set.

## D5 — Load-time consistency repair: lazy repair-and-restamp

The invariant is **self-consistency of the loaded snapshot**, not fidelity to a live
registry. On load, capture a single timestamp `T_load`.

If a runtime contradiction surfaces — e.g. the snapshot's enumeration lists subkey `X`
but opening `X` fails — repair the in-memory model **lazily, on the read that exposes the
contradiction**: drop `X` from the enumeration and set that key's
`last_write_time = T_load`. Do **not** re-query the underlying real platform. The version
stamp advances precisely because the contents changed, so the invariant from D4 holds and
subsequent observations agree (re-enumerate → `X` gone, stamp legitimately newer).

## D6 — Logging is a side diagnostic, never part of any persisted artifact

Logging records the requested-vs-done trace and is purely diagnostic. It is the one layer
useful **injected at almost any depth** (a tap between any two layers), so it is not a
fixed outermost decorator in the intended design.

The persisted state must **not** carry the log. The current logging layer writes a
`<Log>` element into the saved `<Platform>`; that is wrong by this design and must stop.
Logging output belongs in a side artifact, not in any persistence form.

## D7 — Journaling owns ordered replay (separate, deferred capability)

Ordered replay (replaying recorded operations in sequence) is a **journaling**
responsibility, not logging and not the buffered snapshot. It is a separate artifact and
a capability we want available but do not currently require; it is deferred.

## D8 — Fault injection is a counted-rule script

Fault injection is declarative and stateful: a separate input artifact (not part of the
saved `<Platform>`) maps a rule — (operation type, path/pattern, **Nth-occurrence
counter**) — to an action (status / error code). Matching is counted per rule, so e.g.
"the third open of X fails with out-of-resources" is expressible. Consumed by the
fault-injecting layer.

---

## D9 — Filesystem is the second isolation surface; same decorator stack

The filesystem surface reuses D1's architecture wholesale rather than inventing a parallel
one. `iplatform` gains a `get_filesystem()` accessor (beside `get_registry()`) returning an
`ifilesystem`; each existing decorator — pass-through, buffered, redirecting, logging,
journaling, fault — grows a *filesystem facet* alongside its registry facet. Persistence
adds a `<Filesystem>` child to the surface-neutral `<Platform>` element next to `<Registry>`.
The buffered snapshot decisions (D2–D5) and the side-artifact decisions (D6–D8) apply
unchanged in spirit, reinterpreted for filesystem nodes (D13) and subject to the deferred
content limitation (D14). What is genuinely different from the registry surface is confined
to the path/root model (D10, D11), the case-handling rule (D12), and the unified namespace
(D13); everything else is the registry pattern.

The public factory (`make_platform_interface` / `make_platform`) exposes a **single**
redirection table, and that one table is applied to the **whole platform surface**: the
`redirecting::platform` is constructed with the same prefix map installed for both its
registry facet and its filesystem facet. Each facet only matches paths of its own shape, so
a registry-shaped rule (`Software\…`) is inert against filesystem paths and a filesystem-shaped
rule (`Users\Public\…`) is inert against registry paths; callers needing independent registry
vs. filesystem maps drop to the interface-level `redirecting::platform` constructor, which takes
the two tables separately. (Wiring the single table only into the registry facet — the historical
default — silently dropped filesystem redirection, since the filesystem table defaulted to empty.)

## D10 — Filesystem path & root model (`file_path`): open-ended roots

`file_path` mirrors `key_path` (our own path type, not `std::filesystem::path`), but its
root is **open-ended**, not a closed enum. Registry roots are a fixed `predefined_key` set;
filesystem roots are an unbounded family, so a `file_path` carries a `file_root` value =
*kind discriminant + root text*:

- Windows: drive (`C:`), UNC share (`\\server\share`), device / Win32 namespace (`\\.\…`),
  extended-length (`\\?\…`, `\\?\UNC\server\share\…`), and rootless (relative).
- POSIX: the single `/` root, and rootless (relative).

A `file_path` is an optional `file_root` plus normalized relative segments; absence of a
root means the path is relative. This is the structural analogue of `key_path`'s
`(optional<predefined_key> root, relative value)` split — only the root type changes from a
closed enum to an open value.

## D11 — Filesystem path canonicalization

Canonicalization differs from the registry's single-`\`-delimiter rule:

- **Separators.** Accept both `\` and `/` on Windows and normalize to `\`; POSIX uses `/`
  only.
- **Dot segments / collapsing.** Collapse repeated separators, strip a trailing separator
  (except a bare root), and resolve `.` / `..` lexically (a `..` underflow past the root is
  rejected, not silently clamped).
- **`\\?\` suppresses normalization.** Win32 does **not** normalize extended-length paths:
  no `/`→`\`, no `.`/`..` collapsing, no separator de-duplication. Our canonicalizer mirrors
  that — when the `\\?\` (or `\\?\UNC\`) prefix is present, only the prefix is recognized and
  the remainder is preserved **verbatim**. This is a correctness requirement, not cosmetics:
  `\\?\C:\a\..\b` denotes a literally different object than `C:\a\..\b`.
- Canonicalization never changes case (see D12).

## D12 — Case-insensitivity via ordinal sort keys, never case canonicalization

Names are stored and round-tripped in their **original case**. Equality and ordering for
lookup are computed by ordinal case-insensitive comparison — `CompareStringOrdinal(…, TRUE)`,
which is exactly what the existing `m::case_insensitive_less` comparator (already used by the
registry buffered/redirecting overlays) does — optionally via a precomputed *norm_ignorecase
sort key* per name segment so map lookups don't re-fold on every comparison. We never
lower-case or upper-case the stored filename: doing so would discard the on-disk display
casing the OS preserves. POSIX comparison is ordinal **case-sensitive**; the sort-key
abstraction selects the comparator by surface/platform.

Per-directory case sensitivity (Windows 10+ `FILE_CASE_SENSITIVE_DIR`) is acknowledged but
out of scope: the model assumes one case mode per surface. Refining to per-directory mode is
a deferred refinement, noted here so its absence is intentional rather than an oversight.

## D13 — Unified entry namespace: a node is a directory xor a file

Unlike the registry — where a key's subkeys and values occupy **separate** namespaces (a key
may simultaneously have a subkey `foo` and a value `foo`) — every filesystem I know of gives a
directory a single **unified** child-name namespace: each name resolves to exactly one node,
which is either a subdirectory (container) or a file (leaf). The interfaces reflect this:

- `ifilesystem` → `open_root(file_root)` → `idirectory` (the analogue of
  `open_predefined_key` → `ikey`).
- `idirectory`: create/open directory, create/open file, remove (delete a child by name),
  `delete_tree`, rename/move, enumerate entries, `query_information`.
- `ifile`: `query_information` (size, timestamps, attributes). Content/streams are deferred
  (D14).

Enumeration returns directories and files interleaved in one ordered set — the unified
namespace — each tagged with its node kind. A move within the unified namespace is a single
re-keying of the entry, not the registry's separate rename-key vs delete-value operations.

## D14 — Stream content & alternate-stream sub-namespace are deferred

For now a file is modeled as a **named node with metadata only**; we do **not** model its
data-stream bytes nor the alternate-data-stream sub-namespace (`file:stream`). This is the
"kind of incorrect but deferred" trade-off taken deliberately:

- The buffered filesystem surface captures the **namespace and metadata** (which names exist,
  node kinds, attributes, timestamps, sizes) as a sealed whole-node snapshot, but a sealed
  snapshot **cannot serve file content reads** — the acknowledged incorrectness.
- Pass-through / direct still read live content; only buffered *isolation* of content is
  missing.

The deferred milestone (M-FS-STREAMS) closes this in two tractable tiers: (1) the
**namespace level** — treat a file's named streams as their own sub-namespace and model
stream create / delete / rename(move) without byte content (the "portions we may address"
the deferral calls out); (2) full **content** capture/replay of stream bytes, choosing the
capture model and persistence form. Tier 1 may land well before tier 2.

**Terminology note (see D16).** "Stream" in the inception of this feature meant file *data in
general*, **not** specifically the NTFS alternate-data-stream sub-namespace. ADS is a genuine
but secondary concern that happens to fall out of the same modeling; the primary deferred
capability is ordinary file content, and its **intended resolution is redirection-backed, not
byte capture/replay** — see D16. D14 remains the statement of *today's* limitation; D16 states
the shape of its resolution.

---

## D15 — Filesystem change monitor mirrors the registry monitor (detailed notifications)

The filesystem isolation surface exposes a change-notification capability that mirrors
`iregistry_monitor` in shape: `ifilesystem::monitor()` returns an `ifilesystem_monitor`;
callers `register_watch(flags, directory, change_notification)` and receive an opaque
`ifilesystem_monitor_token` whose destruction cancels the watch. The same nine
`register_watch_flags` categories used by `ReadDirectoryChangesW`'s `FILE_NOTIFY_CHANGE_*`
filter are surfaced (subtree, file-name, dir-name, attribute, size, last-write, last-access,
creation, security). The public façade `m::pil::filesystem_monitor` re-declares its own
`register_watch_flags` and maps them bit-for-bit onto the interface enum, exactly as
`registry_monitor` does, so the public header carries no `ifilesystem_monitor` dependency.

**Notifications are detailed, not coalesced-to-key.** Unlike the registry monitor — whose
`on_change` reports only that *something* under the watched key changed — the filesystem
`on_change` carries the `filesystem_change_kind` (added / removed / modified /
renamed_old_name / renamed_new_name) and the affected entry's relative name. This is the
natural granularity `ReadDirectoryChangesW` already provides via the
`FILE_NOTIFY_INFORMATION` `Action` + `FileName` chain, and consumers want it (a rename move
surfaces as the old-name / new-name pair).

**Direct provider is an event + threadpool-wait state machine.** The win32 direct
`filesystem_monitor_token` opens the directory with
`FILE_LIST_DIRECTORY | FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED`, arms a manual-reset
event, and drives a three-state machine (`to_open_directory → to_read_directory_changes →
waiting`) identical in structure to `registry_monitor_token`. On completion it
`GetOverlappedResult`s the OVERLAPPED, decodes the `FILE_NOTIFY_INFORMATION` chain into a
pending-changes vector under the lock, defers delivery through a zero-delay notification timer
(so callbacks fire outside the lock), then re-arms. Access / read-arm failures back off through
the `requeue_directory_access_attempt` / `requeue_change_notification_attempt` hooks (default
500 ms), mirroring the registry monitor's retry contract.

**Destruction quiesces the wait before members die.** The token has an explicit destructor
(not `= default`) that `CancelIoEx`s any in-flight read and then calls `tp_wait::reset()`,
which disarms the wait, waits for any running callback to finish, and closes the wait object —
all while every member the callback touches (buffer, handle, timers) is still alive. It does
**not** additionally call `wait_for_callbacks()`: `reset()` has already nulled the wait handle,
and `wait_for_callbacks()` asserts the handle is non-null, so calling it after `reset()` aborts.

**Facet behavior follows the registry monitor's facet model.** Passthrough and logging simply
forward to the underlying monitor (the logging registry monitor likewise only forwards — it
records nothing for change notifications). Fault and journaling forward `monitor()` to their
underlying. Buffered's `register_watch` is `M_NOT_IMPLEMENTED`: a sealed snapshot has no live
source to watch. Redirecting maps the watched directory public→private on the way in and maps
the reported directory private→public on every notification callback, the same way it maps
registry key paths; the relative `entry_name` is a leaf and passes through unchanged.

---

## D16 — Deferred file content is redirection-backed, not byte-captured

This records the *original intent* behind the deferred content tier (D14), which the
"stream"/"alternate-data-stream" framing under-described. "Stream" meant file **data in
general**, and the intended way to isolate that data is **namespace redirection to a real
backing directory**, not capturing and replaying bytes into the `<Filesystem>` artifact.

The model:

- **Assemble and redirect a subtree at PIL initialization.** Construct a backing directory
  (e.g. a temp directory) populated to *look like* a target subtree — for example a curated
  `%windir%\system32` — and, during PIL init, install a redirection binding so that
  `C:\Windows\system32` resolves into that backing directory. This reuses the existing
  redirecting decorator (D1); it is the filesystem analogue of how registry redirection
  already maps public↔private key paths.
- **Reads are served straight from the backing files.** Because the redirected names resolve
  to real files, content reads are whole-file and natural — there is no capture model to
  choose, no byte serialization, no sealed-snapshot content gap to close.
- **Namespace-level mutation is tracked.** Create, delete, and rename/move of entries within
  the redirected subtree are modeled as overlay entries / tombstones over the backing
  directory, exactly as the buffered namespace already does for the directory tree. This is
  the "partial support" the deferral always meant: deletions and renames are observable and
  isolated.
- **Fine-grained content mutation is deliberately out of scope.** Changing a file's *size*, or
  patching a *subset of bytes in a specific order* (seek-and-overwrite), is **not** modeled.
  A wholesale replacement of a file can be accommodated by swapping the backing node; a
  program that opens a file and rewrites arbitrary byte ranges is outside the isolation model
  by design and either falls through to the backing file or is unsupported. We are explicitly
  *not* building a byte-range/diff content store.

Consequence for D14: the "sealed snapshot cannot serve content" limitation is resolved not by
the tier-2 byte-capture milestone the earlier framing implied, but by pointing the namespace
at a real backing directory and tracking only namespace-level change over it. The alternate-
data-stream sub-namespace (the literal NTFS `file:stream`) remains a secondary, still-deferred
concern under M-FS-STREAMS; it is not the primary content story.

---

## D17 — The `ifile` content accessor: a defaulted positioned whole-file read/write ec-primitive

D16 establishes that deferred file content is **redirection-backed** (reads resolve to real
backing files). D17 records the *interface shape* that exposes those bytes, so the `mwin32`
handle-translation content shim (`mReadFile` / `mWriteFile`, D11) has something to consume.

**Two new ec-primitives on `ifile`: `read_content` and `write_content`.** Each takes a byte
`offset`, a `std::span<std::byte>` buffer, an out `bytes_read` / `bytes_written`, and a
`std::error_code&`, plus the usual flags / result-code / result-flags / `disposition` quartet
and throwing + convenience wrappers — the same primitive+wrapper shape every other PIL verb
uses. The public façade `m::pil::file` gains matching methods.

**They are *defaulted*, not pure virtual.** Unlike the original `ifile` verbs (all pure
virtual), the content accessors carry a base implementation that reports
`std::errc::not_supported` through `ec`. This mirrors the defaulting precedent set by
`iplatform::get_filesystem` (D9) and `get_webcore` (D-HWC-2): a new capability added to a
widely-implemented interface defaults to the inert behavior so that providers, mocks, and test
doubles that do not model content need no edit. `std::errc::not_supported` **is** the documented
"deferred-content" outcome — it is what a sealed buffered snapshot (D14) and the null leaf
return, and what the `mwin32` shim maps to `ERROR_NOT_SUPPORTED`.

**Provider behavior.**
- **direct/win32** serves real bytes: a positioned `ReadFile` / `WriteFile` driven by
  `OVERLAPPED.Offset` on the node's existing OS handle (no separate seek; the offset is
  per-call, so it does not depend on a shared file pointer). A single call is clamped to a
  `DWORD` count and the caller loops for larger transfers. `ERROR_HANDLE_EOF` is a zero-length
  short read, not an error.
- **passthrough / logging / redirecting** override to **forward** to the underlying file (they
  wrap it). **fault** returns the underlying provider's `ifile` unwrapped — it has no `file`
  decorator — so it needs no change; content faulting is out of its scope.
- **buffered** serves **read-through** content for an *unmodified backing* node: a mirrored
  placeholder materialized over a live underlying directory (M-FS-STREAMS-1.4) retains the live
  underlying `ifile` handle, and `read_content` forwards to it so whole-file reads resolve to the
  real backing bytes (this is the content half of the D16 binding, reached through the overlay).
  A node with **no** backing handle — a sealed snapshot (D14) or a created / renamed overlay
  entry — inherits the inert default and reports `not_supported`. `write_content` is **never**
  forwarded by buffered: the shared backing is never mutated through the overlay, so writes stay
  `not_supported` (namespace mutation is isolated in the overlay; content mutation of the backing
  is a D16 non-goal).

**Write is whole-file only (D16 non-goal enforcement).** `write_content` models **whole-file
replacement**: a write at offset 0 that sets the file's extent (direct/win32 follows the
positioned `WriteFile` with `SetEndOfFile`). A write whose `offset` is non-zero — a partial /
mid-file overwrite — is rejected with `std::errc::not_supported`. We deliberately do **not**
build a byte-range / diff content store (D16); arbitrary seek-and-overwrite is outside the
isolation model.

---

## D17 — Captured entries carry the host's alternate (8.3 short) name as a lookup alias

The buffered overlay captures a directory's children by enumeration (D3), which yields each
child's **long** name, and keys them in the ordinal case-insensitive map (D12). But a host
path supplied to `open_directory` may legitimately address an intermediate component by its
**alternate (8.3 short) name** rather than its long name — most commonly because
`std::filesystem::temp_directory_path()` returns a short form when a path component exceeds
eight characters (e.g. a CI runner's `%TEMP%` = `C:\Users\RUNNER~1\…` aliasing `runneradmin`).
The exact long-name lookup then misses and the open fails with "no such file or directory",
even though the live OS would resolve the alias.

Our specified behavior: **an enumerated entry may be addressed by either its primary name or
its captured alternate name.** We capture the alternate name alongside the primary one
(`directory_entry::m_short_name`, empty when the host reports none), carry it on the buffered
`entry_node`, and persist it as the `short_name` XML attribute so a **sealed** snapshot — which
has no live underlying to consult — resolves the alias too. On an exact-match miss,
`open_directory` scans for a non-deleted entry whose alternate name matches the requested leaf
(same ordinal case-insensitive comparison as the primary key, D12). The miss path is the only
place the O(n) scan runs, and only when the exact key was absent, so the common case keeps its
map-lookup cost.

This is the cross-platform "alternate alias" concept, not a Windows-only hack: POSIX surfaces
report no alternate name and leave the field empty, so the scan never matches and behavior is
unchanged there. On Win32 the alias comes from `WIN32_FIND_DATAW::cAlternateFileName`, which
requires enumerating with `FindExInfoStandard` (the previously-used `FindExInfoBasic`
suppresses it). Case-insensitivity itself is **not** the gap here (the map already folds case
per D12); only the 8.3 alias was missing. Per-name multiple aliases are not modeled — a single
alternate name matches what every relevant host surface reports.

---

## D-HWC-1 — Hostable Web Core is an *engine* surface, composed from state surfaces

Every PIL surface to date (registry, filesystem) models **persistent named state**, which is
why the buffered / journaling / snapshot decorators (D1–D9) make sense for it. Hostable Web Core
(HWC) is fundamentally different: `hwebcore.dll` is a **live request-processing engine** exposed
through three flat C entry points (verified against the Windows SDK `um/hwebcore.h`):

| Entry (`PFN_*`) | Prototype | Returns |
|---|---|---|
| `WebCoreActivate` | `(PCWSTR pszAppHostConfigFile, PCWSTR pszRootWebConfigFile, PCWSTR pszInstanceName)` | `HRESULT` |
| `WebCoreShutdown` | `(DWORD fImmediate)` | `HRESULT` |
| `WebCoreSetMetadata` | `(PCWSTR pszMetadataType, PCWSTR pszValue)` | `HRESULT` |

The engine has **no persistent state of its own** to snapshot: its behavior is wholly determined
by the *inputs it reads* (`applicationHost.config`, `web.config`, content roots, some registry
keys) and the *network edge* it binds (`http.sys`). Therefore HWC isolation is mostly
**composition, not a new state model**: if the host process's filesystem and registry surfaces
are already PIL-isolated, an HWC instance activated in-process inherits that isolation for its
config reads. The new surface is thin — it owns **activation lifecycle**, **config projection**,
and the **network edge**.

The decorator stack maps onto HWC as: pass-through activates the real engine against the active
platform; logging traces activate / shutdown / set_metadata; fault injects `WebCoreActivate`
HRESULT failures by the D8 counted-rule model; **buffered / journaling are `M_NOT_IMPLEMENTED`**
(the engine is not snapshotted — isolation of its *inputs* is delegated to the filesystem /
registry surfaces, the same reasoning that makes the buffered filesystem *monitor*
`M_NOT_IMPLEMENTED` in D15); redirecting maps the config `file_path`s public↔private.

## D-HWC-2 — `iwebcore` is added to `iplatform` with a default null provider

A third surface accessor `iplatform::get_webcore(get_webcore_flags, std::shared_ptr<iwebcore>&)`
is added, with a **default that yields a null provider** whose operations are `M_NOT_IMPLEMENTED`
— mirroring exactly how `get_filesystem` was introduced (D9) so existing registry-only and
filesystem providers need no change; only the direct/Windows platform overrides it.

The surface interfaces live in a new `webcore_interfaces.h`:
- `iwebcore_instance` — an opaque activation token whose destruction shuts the instance down
  (RAII, like `ifilesystem_monitor_token`).
- `iwebcore` — `activate(activate_flags, activation_request const&, std::unique_ptr<iwebcore_instance>&, std::error_code&)`
  and `set_metadata(...)`. The `activation_request` carries the app-host config and optional
  root-web config as **`file_path` values** (paths *in the isolated filesystem*, not raw OS
  paths) plus the instance name — this is what wires HWC's config reads to the isolated FS.

Error model follows PIL: `std::error_code&` is the non-throwing **primitive** channel each
provider implements; `disposition` carries only **contractual non-success** (e.g.
`already_activated`, the HWC `ERROR_SERVICE_ALREADY_RUNNING` contract), never errors. A thin
throwing overload wraps the ec primitive.

## D-HWC-3 — engine bound via `LoadLibraryExW`, never statically imported

The direct/Windows webcore provider resolves the engine **purely at runtime** — no import lib, no
`__declspec(dllimport)`, no link-time edge on anything IIS.

**The engine lives in `%windir%\system32\inetsrv\hwebcore.dll`, NOT `system32` directly, and it
has a dependency closure of sibling DLLs in that same `inetsrv` folder.** This was verified on a
machine with the `IIS-HostableWebCore` feature installed: a bare-name
`LoadLibraryExW(L"hwebcore.dll", …, LOAD_LIBRARY_SEARCH_SYSTEM32)` fails with
`ERROR_MOD_NOT_FOUND` (the module is not in `system32`), and even a full-path load fails the same
way until `inetsrv` is on the **dependency** search path (the engine's siblings — `iiscore.dll`,
`nativerd.dll`, etc. — live there). So the binding is:

```cpp
// resolve the absolute system path: GetSystemDirectoryW() + L"\\inetsrv\\hwebcore.dll"
// add inetsrv to the dependency search, then load by full path:
::AddDllDirectory(inetsrv_dir);                // or SetDllDirectoryW(inetsrv_dir)
auto h = ::LoadLibraryExW(full_inetsrv_path, nullptr,
                          LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
```

Loading by the **absolute system `inetsrv` path** (never a bare or relative name) is what hardens
against an app-dir search-order hijack. The exact search-flag combination is finalized in the
`M-HWC-DIRECT` C++ provider against the live engine — a one-line incantation is **not** assumed;
the provider owns getting the dependency closure to resolve.

The three `GetProcAddress` results (`WebCoreActivate` / `WebCoreShutdown` / `WebCoreSetMetadata`)
are the **injectable seam**: a fake engine in unit tests is just a different function-pointer
triple, so the provider is fully testable with no IIS feature installed. The module handle is
owned by the provider (load once on first `activate`, `FreeLibrary` on provider teardown), because
HWC is process-singleton anyway (D-HWC-5). Because the binding is `LoadLibraryExW`-at-activate,
`mwin32` gains no link-time dependency on IIS.

## D-HWC-4 — isolate config/registry reads by materialization (default) or interception (opt-in)

`hwebcore.dll` is real native code that calls the real OS; it does **not** see the mwin32
registry shim and cannot read from a buffered in-memory filesystem. Two strategies bridge the
gap, chosen per-config:

- **Materialization (default).** On `activate`, resolve the config `file_path` through the
  *isolated* `ifilesystem`, read its bytes, create a real per-instance temp dir, project every
  `physicalPath` / content root the config references from the isolated FS into that temp dir,
  rewrite the paths, write the rewritten `applicationHost.config` to a **real** path, and call
  `WebCoreActivate` against it. The token destructor shuts down and deletes the projection. This
  is a deliberate, documented **isolation boundary**: at the moment control passes to un-shimmed
  native code, isolation necessarily becomes concrete (the HWC analogue of the D14 acknowledged
  trade-off).
- **Interception (opt-in, D-HWC-7).** Instead of making the inputs real, intercept the engine's
  outbound `Reg*` / `CreateFileW` / `FindFirstFileW` calls **module-scoped to `hwebcore.dll`** and
  route them into the active PIL registry / filesystem surfaces — dropping materialization
  entirely and giving an exact "what the engine actually touched" trace through the logging facet.

## D-HWC-5 — single activation per process, enforced on the session

The HWC contract is one activation per process (`WebCoreActivate` returns
`HRESULT_FROM_WIN32(ERROR_SERVICE_ALREADY_RUNNING)` on a second call; `WebCoreShutdown` returns
`ERROR_SERVICE_NOT_ACTIVE` when nothing is active). The mwin32 process-wide session holds the one
`iwebcore_instance`; a second activation returns the real HWC failure shape rather than minting a
second token. Unlike registry handles, HWC has **no handle** in its ABI, so the `handle_table` is
not involved — the session simply owns the single instance token, matching the engine's
process-singleton model.

## D-HWC-6 — network edge is the deferred `ihttp_listener` surface via namespace redirection

`hwebcore.dll` reaches the network through the **HTTP Server API** (`HttpInitialize`,
`HttpCreateServerSession` / `HttpCreateHttpHandle`, `HttpAddUrlToUrlGroup` / `HttpAddUrl`, then
receive / send). Namespace redirection means: intercept exactly those calls on the engine module
and **remap the URL namespace and port into a private sandbox** before they reach `http.sys`, so
production names / ports are never reserved on the box. Expressed as a deferred `ihttp_listener`
surface with two tiers:

- **Tier A — real `http.sys`, private namespace.** Rewrite host:port to loopback + an ephemeral
  port and synthesize the URL-ACL / cert binding for *that private prefix*; clients talk to the
  privatized address. Good enough to actually serve requests without reserving production URLs.
- **Tier B — fake `http.sys`.** Intercept the receive / send HTTP Server API too and feed requests
  from an in-process queue — no `http.sys`, no admin, no URL ACL, fully deterministic. This is the
  unit-test edge and the strongest "fake privatized addresses and ports."

`.pilcfg` carries the mapping table (`webcore.endpoints`: public ↔ private). This replaces the
weaker "ephemeral port + URL ACL" hand-wave with a concrete contract.

## D-HWC-7 — Detours is an opt-in, module-scoped, off-by-default complementary envelope

The repository generally eschews runtime interception (Detours), preferring the link-time alias
mechanism (mwin32 D8). HWC is the bounded exception: the engine is a black box we deliberately do
**not** link, so intercepting its outbound calls is the only way to resolve them against PIL
("what happens under the hood" of the engine). The deviation is constrained:

- Hooks are installed **only on `hwebcore.dll`'s own IAT / delay-IAT** (module-scoped via the
  `HMODULE` from D-HWC-3), never process-wide inline hooks — we intercept *the engine's* calls and
  nothing else.
- The whole envelope is gated behind a `webcore.interception` mode in `.pilcfg` and is **off by
  default**; passthrough / materialization (D-HWC-4) remains the default path.

This keeps the experiment controlled and bounded to the HWC surface rather than ambient hooking.

---

## Implementation notes

### M-PS-1 audit — current mirror & serialization model (as of 2026-06-14)

How the buffered layer mirrors and serializes today, and the concrete delta the M-PS
milestone must close to reach D2/D3 (whole-key sealed snapshot).

**Mirror model (lazy, name/type-only at materialization).** When a buffered `key` wraps
an underlying key, `key::initialize_overlay`
(`src/buffered/registry_key_key_operations.cpp`,
`src/buffered/registry_key_value_operations.cpp`) eagerly records:
- subkey **names** as `key_node{m_key={}, m_last_write_time={}, m_mirrored=true}` — name
  only, no contents, no timestamp;
- value **names + types** as `value_node{m_reg_value_type=type, m_value=nullopt}` — no data.

Contents are loaded lazily: a value's bytes enter `m_value` only on a read
(`load_value_if_not_present` via `get_value`/`get_value_size`); a subkey's `m_key`
materializes only on `open_key`/`create_key`. The key's own `m_last_write_time` is left at
`time_point_type::min` for mirrored keys (only created/loaded keys set it).

**Serialization (`key::save_xml`, `registry::save_xml`).** Persists only materialized or
modified state:
- values emit a `<Value>` only if read (`m_value.has_value()`) or deleted (tombstone);
  enumerated-but-unread value names are dropped;
- subkeys emit a `<Key>` only if opened (`m_key` set) or deleted; enumerated-but-unopened
  subkey names are dropped;
- key **metadata is never written** — no `last_write_time` attribute. Load
  (`load_children_xml`, `registry::load_xml`) reconstructs every key at
  `time_point_type::min`.

**Concrete delta for M-PS-2/3/4:**
1. Persist & restore key metadata — `last_write_time` is currently absent on both sides.
2. Capture & persist the full **subkey-name list**, including enumerated-but-unopened
   names, so a loaded snapshot reproduces observed enumeration (D3).
3. Capture & persist **all values whole** for a touched key (eager load on capture per
   D4), not only those that were read.
4. Loaded snapshot then becomes a **sealed world** needing no fall-through to a real
   platform (D2). Mirrored placeholders must no longer be silently dropped at save.

### M-PS-2 — whole-key capture on touch (as of 2026-06-15)

`key::initialize_overlay` now performs an eager whole-key capture: it brackets the
capture with `last_write_time` reads (T0 before / T1 after), enumerates subkey names and
value names+types, eagerly loads all value bytes (`load_all_mirrored_values`), and stamps
the key's `m_last_write_time = T1`. A torn read (T0 != T1) retries up to
`k_max_capture_attempts` (3); values that vanish between enumeration and load are dropped
(best-effort, D4).

**Underlying-layer fix (mono-repo bug policy).** The win32 direct
`key::query_information_key` retrieved the `FILETIME` from `RegQueryInfoKeyW` but never
converted it — `last_write_time` was always left at `time_point_type::min` (the conversion
line was commented out). Fixed at the source to use
`m::clock_cast<m::pil::clock_type>(FILETIME)` from `m_windows_chrono` (added to the win32
direct link set). Without this, captured metadata is meaningless.

**M-PS-MOCK (implemented 2026-06-15).** The torn-read→retry and vanished-value→drop paths
are now covered deterministically by a controllable mock `ikey`
(`test/Platforms/Windows/mock_ikey.h`) that scripts the capture bracket the real registry
cannot perturb on demand. The mock implements only the read surface capture touches
(`query_information_key`, `enumerate_keys`, `enumerate_value_names_and_types`,
`get_value_size`, `get_value_type`, `get_value`); all mutators throw `m::not_supported`. It
returns a scripted sequence of `last_write_time` values across successive
`query_information_key` calls (so the before/after bracket can be made to disagree, then
agree) and lets any value be flagged "vanished" so its `get_value_size`/`get_value` throw
`m::not_found`. It records the number of capture passes (one
`enumerate_value_names_and_types` sweep per attempt) so tests can assert the bounded retry
fired the expected number of times. Tests (`test/Platforms/Windows/test_buffered_mock.cpp`):
a torn read that stabilizes after one retry (2 passes, settles on the stable stamp); an
ever-changing key that stops at the `k_max_capture_attempts == 3` bound; and a value that
vanishes between enumeration and load being dropped while its sibling survives. The mock is
reusable for later milestones. The delivered M-PS-2 tests remain, covering eager whole-value
capture and metadata capture against the real registry.

### M-PS-3 — whole-mirror serialization (as of 2026-06-15)

`key::save_xml` now emits a whole-key snapshot (D2, D3): the key's own `last_write_time`
(as a raw tick count attribute, emitted only when not `min`), every captured `<Value>`, and
a `<Key>` child for **every** child subkey name — including mirrored-but-unopened
placeholders, which contribute only their name (their contents were never captured per the
non-recursive D3 rule). Materialized subkeys still recurse into a nested whole `<Key>`;
deleted entries remain tombstones. `key::load_children_xml` restores `m_last_write_time`
from the `last_write_time` attribute (absent → `min`, covering name-only placeholders and
older artifacts).

The timestamp is serialized as the integer tick count of
`time_since_epoch()` (lossless round-trip); it doubles as the version stamp used by lazy
consistency repair on load (D5).

**Test note.** `BufferedSave.OverlayKeysAndValuesAreSerialized` previously asserted (in a
comment) that mirrored-but-untouched subkeys must *not* appear; that is false under the
whole-key model. The comment was corrected and a new
`BufferedSave.ObservedKeyMetadataAndSubkeyNamesSerialized` test asserts a merely-observed
key serializes its metadata and child subkey names.

**Resolved flaky abort (was pre-existing).** `test_win32_registry` used to abort
nondeterministically at the `DirectRegistryMonitoring.MonitorKey` →
`TestLoggingRegistry.TryCreatingKey` boundary (~40–60% of full-suite runs). Root cause: the
direct `registry_monitor_token` had a `= default` destructor, so member destruction ran in
reverse declaration order and destroyed its two timers **before** `m_tp_wait` drained. Its
registry-notification wait callback (`on_registry_notification` → `drive_state`) schedules
those timers, so a registry change delivered during the teardown window dereferenced
already-destroyed timer members → `abort()`. Fixed by giving the token the same treatment as
`filesystem_monitor_token`: an `m_shutting_down` flag set under the lock, an explicit
destructor that drains `m_tp_wait` before the timers are reset, and an early-return guard in
`drive_state` (and the two timer lambdas) so no in-flight callback re-arms past the drain.
Verified with 25 clean full-suite runs in each of debug and release.

### M-PS-4 — sealed load (as of 2026-06-15)

The sealed-world load machinery was effectively delivered by M-PS-3's `load_children_xml`
change (metadata, values, and subkey-name lists all restore from the artifact) plus the
pre-existing no-underlying snapshot `registry::load_xml` / snapshot `platform` constructor.
M-PS-4 is therefore a *verification* milestone rather than new code: it adds
`BufferedSave.ObservedKeyReadableFromSealedSnapshot`, which captures a real key through the
buffered layer, saves, **deletes the real key**, then loads the snapshot and reads the
captured values back — proving there is no fall-through to a live registry.

### M-PS-5 — lazy consistency repair (as of 2026-06-15)

Implements D5. Name-only placeholders (subkey names observed during eager capture but
whose contents were never materialized) are now serialized with an explicit
`mirrored="true"` attribute, so the loader can distinguish them from fully-captured empty
keys. On load they are restored as unmaterialized mirrored `key_node`s (enumerable but not
openable), rather than fabricated empty keys.

`registry::load_xml` captures a single `T_load = clock_type::now()` for the whole snapshot
and threads it into every `key::load_children_xml`, which stores it as `m_load_stamp`. When
`open_key` reaches a mirrored, unmaterialized placeholder and there is **no underlying
registry** (the sealed world), it performs the repair: it erases the entry from `m_keys`
and sets the parent's `m_last_write_time = m_load_stamp`, then returns key-not-found
(honoring `tolerate_not_found`/`ec`). It never consults a live platform. `create_key`'s
analogous branch instead materializes a fresh empty key in the sealed world, preserving
create-or-open semantics. The version stamp advances precisely because the enumeration
changed (D4 invariant), so re-enumeration after repair is self-consistent. Covered by
`BufferedSave.NameOnlySubkeyRepairedAndRestampedOnOpen`. The M-PS-4 test's child check was
updated to assert the name *enumerates* (rather than opens), since the placeholder now
correctly fails to open in the sealed world.

### M-PS-6 — end-to-end integration (as of 2026-06-15)

Completes the M-PS milestone with `BufferedSave.EndToEndSealedSnapshotReproducesObservations`.
It stages a representative tree (values of several types plus nested subkeys Alpha,
Beta\Gamma), observes it through the buffered layer, applies overlay edits (add `vnew`,
delete `doomed`, create `Delta`), saves, deletes the live tree, and reloads as a sealed
snapshot. The test asserts the sealed world reproduces both captured and overlay-written
state, does not resurrect the deleted value, serves the nested and overlay-created subkeys,
and returns a stable subkey enumeration across repeated reads — exercising D2–D5 together
with no fall-through to a live registry.

### M-LOG-OUT-1 — logging save is pass-through (as of 2026-06-15)

Implements the persistence half of D6. `logging::platform::save` no longer appends a `<Log>`
element to the saved `<Platform>`; it now forwards unchanged to the underlying platform.
The diagnostic log object (`m_log`) is still populated during operation but is never written
into any persisted artifact. The separate side-artifact channel for obtaining the trace is
M-LOG-OUT-2.

### M-LOG-OUT-2 — diagnostic log side artifact (as of 2026-06-15)

Completes D6. `iplatform` gains a `save_diagnostic_log(save_flags, pugi::xml_node&)` virtual
with a base no-op default (layers that record no trace leave the node untouched). The
logging layer overrides it to append its `<Log>` (the requested-vs-done trace) into the
caller's node. The public `m::pil::platform::save_diagnostic_log(path)` writes a separate
file rooted at `<DiagnosticLog>` — structurally disjoint from `save`'s `<Platform>`, so the
trace can never leak into persisted state. Covered by
`TestLoggingRegistry.DiagnosticLogIsSideArtifactNotInPersistedPlatform`, which asserts the
saved `<Platform>` contains no `<Log>`/log entries while the side artifact does. The
default no-op keeps the channel meaningful even when no logging layer is present (the
artifact is well-formed but empty). Routing the tap to non-outermost depths is M-LOG-FLOAT.

### M-LOG-FLOAT — injectable / floating logging tap (as of 2026-06-15)

Completes the injectable half of D6. The wrapping API is simply: construct
`logging::platform` over *any* `iplatform` (not only the outermost), and the tap records the
mutations passing through it at that depth. For the trace to remain reachable from the top of
the stack, every transparent decorator now forwards `save_diagnostic_log` down to its
underlying (`buffered`, `redirecting`, `passthrough`); the `logging` override appends its own
`<Log>` and then forwards, so stacked taps each contribute their slice. A decorator with no
underlying (a sealed `buffered` snapshot leaf) returns the no-op disposition, terminating the
chain.

Record shape (requested-vs-done): each `<Log>` holds `Registry.*` mutation entries
(`Registry.CreateKey`, `Registry.SetValue`, `Registry.DeleteKey`, `Registry.DeleteTree`,
`Registry.RenameKey`, `Registry.DeleteValue`) carrying the requested arguments plus a
`disposition` attribute recording the done result. Only mutations are traced; reads are pure
pass-through.

`LoggingFloat.TapCapturesAtAnyDepthWithoutAlteringBehavior` issues identical operations
against one sealed snapshot through two stacks differing only in tap depth — `logging`
directly above the leaf, and `logging` beneath a transparent `passthrough` — and asserts both
diagnostic logs capture the create/set trace (the tap floats and stays reachable) while the
read-back values are identical (behavior is unaltered by tap placement).

### M-JOURNAL-1 — journaling decorator and ordered verb-stream artifact (as of 2026-06-15)

Realizes D7. The `journaling` decorator (`src/journaling/`) is modeled on `logging`: a
transparent stack of `platform` / `registry` / `key` wrappers that forward every operation to
the underlying layer unchanged. As each mutating call passes through, the `key` wrapper
appends a verb entry to a shared, mutex-guarded `journal` (an ordered `std::deque`), recording
the operations in exact document order.

Scope — mutations only. The journal records the six mutating verbs (`CreateKey`, `DeleteKey`,
`DeleteTree`, `RenameKey`, `DeleteValue`, `SetValue`). Reads are pure pass-through and are not
journaled: ordered replay onto a base world to reach observable equivalence (M-JOURNAL-3)
needs only the mutations, so journaling reads would be dead weight in the artifact.

Encoding — lossless and replay-focused, owned by us (design autonomy). Distinct from logging's
human-readable requested-vs-done rendering, each entry stores exactly what replay needs: the
base key's absolute path, the verb's arguments, and for `SetValue` the value's *raw*
`reg_value_type` (numeric) plus its bytes as lower-case hex. This is a deliberately exact,
machine-replayable shape; changing the hex alphabet or the type/data attributes is a breaking
change to the artifact.

Separate artifact (not in `<Platform>`). Per D7 the journal is its own artifact, never folded
into the persisted snapshot. `journaling::platform::save` is therefore a transparent
pass-through (mirroring the M-LOG-OUT decision for the diagnostic log), and the recorded
stream is emitted on demand through `platform::save_journal(node)`, which writes the verb
children under a caller-supplied `<Journal>` root. `save_diagnostic_log` forwards downward so a
logging tap placed beneath journaling remains reachable from the top (D6).

### M-JOURNAL-2 — ordered replay onto a base world (as of 2026-06-15)

The free function `replay(journal_node, target_registry)` (`src/journaling/replay.cpp`) reapplies
the recorded verb stream in document order. For each child element it resolves the base key the
operation was invoked on (parse the absolute `key` attribute → open its predefined root →
`create_key` the relative path, which is idempotent so it is safe whether or not the key already
exists in the target), then dispatches on the element name to reissue the verb against that base
key using the interface convenience helpers (`create_key`, `delete_key`, `delete_tree`,
`rename_key`, `delete_value`, and the four-argument `set_value`).

`SetValue` round-trips losslessly: the numeric `reg_value_type` is read back from `type` and the
bytes are decoded from the lower-case hex `data` attribute by `hex_to_bytes` (the inverse of the
record-side `bytes_to_hex`). A malformed journal (odd-length or non-hex data, a key path with no
predefined root, or an unknown element name) throws rather than silently replaying garbage.
Replay targets the `iregistry` interface, so it works against any world — a fresh snapshot leaf,
a live platform, or another decorated stack.

### M-JOURNAL-3 — record/replay observable-equivalence test (as of 2026-06-15)

`Journaling.RecordReplayProducesObservableEquivalence` records an order-sensitive mutation
sequence through `journaling::platform` over a sealed buffered snapshot leaf — repeated
`SetValue` on the same name (last writer wins: 1 then 2), a `SetValue`/`DeleteValue` pair, a
nested key with its own value, and a create-then-delete key — then captures the recorded stream
into a standalone `<Journal>` document via `save_journal`. It replays that journal onto a
*fresh* leaf built from the same fixture and asserts the replayed world matches what the source
produced (final value 2, deleted value absent, nested value present, deleted key gone).

Resolving the base key during replay descends the relative path one segment at a time, because
the buffered base world's `create_key` accepts only single-segment names; a single multi-segment
`create_key` would be rejected.

The sequence includes a non-empty subtree (`ToDelete` holding a value) removed wholesale via
`delete_tree`, so the test drives the `DeleteTree` verb against the buffered leaf end to end
(record → `<Journal>` → replay). This relies on buffered `delete_tree` being implemented; see
M-BUFTREE.

### M-BUFTREE-1 — buffered delete_tree (as of 2026-06-15)

Implements `buffered::key::delete_tree` (`src/buffered/registry_key_key_operations.cpp`), which
previously threw `not_implemented`. The overlay model makes whole-subtree deletion a single
tombstone rather than a literal recursion: each `key` is a self-contained snapshot whose subkeys
live as `key_node` entries, and `open_key` / `try_open_key` already honor a node's `m_deleted`
flag. Tombstoning the named subkey's node (`m_key.reset()`, `m_deleted = true`,
`m_mirrored = false`) therefore hides that subkey and *every* descendant at once — a materialized
child becomes unreachable, and a mirrored-but-unmaterialized subtree in the underlying registry
is shadowed by the tombstone — without walking the tree. Unlike `delete_key`, there is no
"subkey must be empty" precondition, matching `RegDeleteTree` semantics.

Two argument shapes, mirroring the win32 `ikey::delete_tree` contract so the buffered
implementation is a faithful peer of the win32 one:
- Named subkey: tombstone that subkey node (the case above). Multi-segment paths are rejected
  with `invalid_parameter`, matching `delete_key`; a missing/already-tombstoned subkey throws
  `not_found`.
- No name (or empty name): clear the key's *contents* — tombstone all `m_keys` nodes and all
  `m_values` — while leaving the key itself in place.

The no-name branch exists for interface-contract parity with win32 (and is the path the
journaling replay layer takes for a subKey-less `DeleteTree` entry); it is not reachable through
the friendly `key` API today, which exposes no nullopt overload.

Tests: `BufferedDeleteTree.NamedSubtreeWithDescendantsIsRemoved` (a non-empty subtree deleted
through the friendly buffered overlay vanishes wholesale — something `delete_key` cannot do), and
the M-JOURNAL-3 record/replay test now drives `DeleteTree` against the buffered leaf rather than
working around the former gap with `delete_key`.

Known limitation (out of scope): `enumerate_keys` does not skip tombstoned nodes, so a deleted
subkey can still surface by index even though `open_key` refuses it. `try_open_key`-based
observation (used by the tests) reflects the deletion correctly.

### M-FAULT-1 — fault-script artifact and grammar (as of 2026-06-15)

Defines the declarative fault artifact described by D8, in `src/fault/fault.h` /
`src/fault/fault_script.cpp` (namespace `m::pil::impl::fault`). The artifact is a separate XML
input — never part of the saved `<Platform>` — parsed by `parse_fault_script(pugi::xml_node)`
into a `fault_script` (a vector of `fault_rule` under a mutex).

Grammar: a `<FaultScript>` element with zero or more `<Rule>` children, each carrying
attributes `operation` (one of the `fault_operation` verbs: create_key, open_key, delete_key,
delete_tree, rename_key, set_value, delete_value, get_value), `path` (the absolute,
root-prefixed key path the rule targets), an optional `valueName` (for value operations),
`occurrence` (the 1-based Nth-match counter, must be ≥ 1), and `action` (one of the
`fault_action` outcomes). Unknown verbs/actions, a missing required attribute, or
`occurrence < 1` raise `m::invalid_parameter` at parse time.

Matching is counted and **one-shot on exactly the Nth occurrence**: `fault_rule::match_and_count`
compares the operation verb and a **case-insensitive full absolute-path equality** (registry
semantics — the D8 "pattern" is an exact ci path match, documented as such), requires the value
name to match when the rule specifies one, increments the rule's hit counter on every match, and
returns its action only when `m_hits == m_occurrence`. This satisfies "fires on the Nth and not
before"; it does not refire afterward. `fault_script::check` advances *all* matching rules'
counters (so composed rules stay independent) and, if any fired, raises the mapped exception.

Faults surface as the real foundation `m::` exceptions, so consumers exercise genuine error
paths. The `fault_action` vocabulary maps to `m::not_found`, `m::access_denied`,
`m::out_of_resources`, `m::sharing_violation`, `m::already_exists`, and `m::not_supported`. Two
of these — `access_denied` (the canonical registry failure) and `out_of_resources` (the D8
worked example, resource exhaustion) — were added to the foundation header
`src/include/m/utility/exception.h` in this item, following the mono-repo "own the layer" and
design-autonomy rules rather than overloading an unrelated existing exception.

### M-FAULT-2 — fault-injecting decorator (as of 2026-06-15)

The decorator stack (`src/fault/platform.cpp`, `registry.cpp`, `registry_key.cpp`) is modeled on
the journaling layer: a single shared state object (the `fault_script`) is threaded
platform → registry → key, the registry caches wrapped predefined keys, and each wrapped `key`
holds the underlying `ikey` plus the shared script. Unlike journaling — which creates its own
`journal` internally — the script is parsed externally and passed in, so `create_platform` takes
`(underlying_platform, script)`.

Each faultable `key` method computes the rule target from `ikey::get_path()` (the wrapper's own
absolute path) and calls `m_script->check(...)` **before** forwarding to `m_key`; if a rule
fires, `check` throws and the underlying layer is never reached. Key-targeting ops compose the
target with `key_path::operator+` (the subkey/old-name, using the `std::optional` overload so a
null name targets the key itself): create_key, delete_key, delete_tree, open_key, rename_key.
Value ops (set_value, delete_value, get_value) pass the wrapper's own path plus the value name
(`value_name.view()`). Structural/read-only ops (enumerate_keys, enumerate_value_names_and_types,
query_information_key, flush, get_value_size, get_value_type, get_path, monitor) forward
transparently and carry no rules.

### M-FAULT-3 — fault layer tests (as of 2026-06-15)

`test/Platforms/Windows/test_fault.cpp` drives the fault layer over a win32-free buffered
snapshot base world (the same fixture approach as the journaling tests): `CountedMatch...`
(3rd open fires, 1st/2nd/4th do not — one-shot on the Nth), `MultipleRulesComposeIndependently`
(two rules on different paths with different actions fire on their own independent counts),
`NonMatchingOperationsPassThroughUnchanged` (a rule on an untouched path leaves create/set/get/open
intact, values round-trip), and `ParsedScriptFiresOnMatchingCreate` (exercises
`parse_fault_script` end to end).

The tests derive every rule path from the live `key::get_path()` of the key under test rather than
hand-writing a `HKEY_*\...` string, and `ParsedScriptFiresOnMatchingCreate` *discovers* the
target path with a throwaway probe platform before building the `<FaultScript>` XML. This is
deliberate: in buffered **snapshot mode** (no live underlying platform) a predefined key is
constructed without a root path, so `get_path()` on HKCU returns an empty path and a hand-authored
`"HKEY_CURRENT_USER\Foo"` rule would not match. Over a live win32 platform `get_path()` carries
the properly rooted absolute path, so authored rules match as intended; the probe keeps the tests
faithful to the decorator's own composition regardless of that snapshot-mode quirk (a buffered
characteristic, out of scope for the fault layer).

### PERSIST-1 — removed legacy file-based buffered save path (as of 2026-06-15)

The buffered layer carried a second, file-based save API parallel to the public node-based
`iplatform::save` and unreachable from it: `buffered::platform::save(persistence_format,
std::filesystem::path)`, the private `buffered::platform::save_xml(m::locked_t,
std::filesystem::path)`, and the `enum class persistence_format { xml }`. It was dead code (no
caller anywhere in the repo) and latently buggy — `save_xml(locked_t, path)` called
`doc.document_element()` on an empty document (a null node) and then `set_name`/appended onto it.

All three artifacts were removed. The sole supported persistence path is the polymorphic
`platform::save(save_flags, save_contents, pugi::xml_node&)` virtual feeding
`registry::save_xml(pugi::xml_node&)`, wrapped by the public `m::pil::platform::save(path, ...)`
in `src/platform.cpp`; load is `create_platform_from_persisted_xml`. `registry::save_xml(node)`
is retained (it is the node-based path's serializer). Nothing needed to be folded forward — the
node-based path already supersedes everything the legacy path attempted.

### M-FAULTCFG-1 — public fault-layer surface (as of 2026-06-15)

The fault layer (D8, M-FAULT-1/2/3) was internal to `m::pil::impl::fault`. This item adds a
public façade in `include/m/pil/fault.h` (implemented in `src/fault_interface.cpp`) so a
consumer — notably the mwin32 shim's `.pilcfg` integration — can build and apply a fault script
without reaching into the impl namespace. The surface mirrors the existing
`make_platform_interface` / `load_platform_interface` factories: construct a script, then layer
it over an `std::shared_ptr<iplatform>` stack.

Public shape (namespace `m::pil`):
- `enum class fault_operation` and `enum class fault_action` — independent public copies of the
  impl vocabularies (same eight operations / six actions). Per the design-autonomy rule the
  public enums are *not* aliases or `static_cast`s of the impl enums; `fault_interface.cpp`
  contains the single `to_impl` switch that maps each value, so a future divergence is a compile
  error at that one site rather than a silent mismatch.
- `class fault_script` — a thin value handle wrapping `std::shared_ptr<impl::fault::fault_script>`
  (impl type forward-declared in the header; only the `.cpp` includes `src/fault/fault.h`).
  `add_rule(operation, key_path, optional<value_name_string_type>, occurrence, action)` builds an
  `impl::fault::fault_rule` and appends it. An internal `get_impl()` accessor exposes the shared
  impl script to `apply_fault_layer` (documented as not part of the stable contract — it exists
  because the public type is a handle over the impl representation). The handle is copyable and
  shares the underlying script, so rules added after a layer is applied still take effect (the
  layer holds the same `shared_ptr`).
- `parse_fault_script(pugi::xml_node const&)` / `load_fault_script(std::filesystem::path const&)`
  — wrap `impl::fault::parse_fault_script`; `load_fault_script` loads the file (document element
  must be `<FaultScript>`) and throws `std::runtime_error` on load failure, deferring parse
  errors (`m::invalid_parameter`) to the shared parser.
- `apply_fault_layer(std::shared_ptr<iplatform> const&, fault_script const&)` — calls
  `impl::fault::create_platform(underlying, script.get_impl())`, returning the wrapped interface.

`include/m/pil/fault.h` includes `<pugixml.hpp>` for the `pugi::xml_node` parameter, consistent
with `platform_interfaces.h` which already exposes pugixml on the public surface.

Tests: `test/Platforms/Windows/test_fault_public.cpp` exercises the façade only (no impl
includes) over a sealed snapshot obtained via the public `load_platform_interface`:
`ProgrammaticRuleFiresOnNthOccurrence`, `ParsedScriptFiresOnMatchingCreate`,
`LoadedScriptFromFileFires` (round-trips a `<FaultScript>` file through `load_fault_script`), and
`NonMatchingOperationsPassThrough`. The same snapshot-mode probe technique as M-FAULT-3 derives
rule paths from a live `get_path()`.



### M-FS-FAULT-1 — filesystem facet of the fault layer (as of 2026-06-16)

Realizes D8 for the filesystem surface, mirroring the registry facet (M-FAULT-1/2/3) the same
way the journaling and logging filesystem facets mirror their registry peers. The fault
vocabulary (`src/fault/fault.h`, namespace `m::pil::impl::fault`) gains seven filesystem
operations alongside the eight registry ones: `create_directory`, `create_file`,
`open_directory`, `open_file`, `remove_entry`, `delete_tree_entry`, `rename_entry`. A single
`<FaultScript>` may therefore mix registry and filesystem rules; `operation_from_string` maps
all fifteen spellings.

Naming: the tree-delete verb is spelled `delete_tree_entry`, **not** `delete_tree`, because the
registry already owns `delete_tree`. Keeping one unambiguous `operation_from_string` map (and
one `fault_operation` enum spanning both domains) is worth the slightly longer name; the public
enum copies the same spelling.

Unified target representation. `fault_rule` previously stored a `key_path` target; it now stores
the **already-normalized native text** as a `std::u16string` (`m_target`). The registry
constructor seeds it from `key_path::native()`, the new filesystem constructor from
`file_path::native()`. Matching is a single private `match_text_and_count` doing the
case-insensitive full-text equality plus the counted/one-shot logic; the two public
`match_and_count` overloads (key_path + optional value name; file_path) and the two `check`
entry points (`check`, `check_filesystem`) delegate to it. This keeps one counting mechanism for
both surfaces without letting either path type's normalization leak into the other — each side
normalizes in its own path type before the text is stored.

Decorator. `src/fault/filesystem.cpp` adds `directory` and `filesystem` decorators modeled on
the journaling filesystem facet. Like journaling's wrappers — and because `idirectory` exposes
no `get_path()` — each `directory` tracks its own absolute `file_path` (`m_absolute_path`):
`filesystem::open_root` seeds it from `file_root::text()`, and every `open_directory` /
`create_directory` child receives `parent.m_absolute_path / segment`. Each faultable verb calls
`m_script->check_filesystem(op, target)` **before** forwarding, so a fired rule throws and the
underlying layer is never reached (and the overlay is left unmutated). Targets:
create/open of a child use `m_absolute_path / path`; `remove_entry` uses `/ name`;
`delete_tree_entry` uses `name ? m_absolute_path / *name : m_absolute_path`; `rename_entry`
matches on the **source** name (`m_absolute_path / old_path`). Returned directories are
re-wrapped so the subtree stays faulted; returned files are forwarded unwrapped (`ifile` carries
no faultable verbs). `enumerate_entries` / `query_information` forward transparently.

`open_root` itself carries no fault verb — it is the un-faulted entry point, mirroring how the
registry facet leaves `open_predefined_key` unfaulted.

Public façade. `include/m/pil/fault.h` / `src/fault_interface.cpp` gain the seven filesystem
values on the public `fault_operation` enum and a second `add_rule` overload taking a
`file_path` target with no value-name parameter (filesystem operations carry no value-name
constraint). Per the design-autonomy rule the public enum is an independent copy; the single
`to_impl` switch in `fault_interface.cpp` maps each new value, so divergence is a compile error
at that one site.

Tests: `test/Platforms/Windows/test_fault_filesystem.cpp` drives the `directory` decorator
directly over a **sealed** buffered overlay (`buffered::directory(md, nullptr)` — no live
underlying), so matching/counting is deterministic and win32-free. Rule targets are formed the
same way the decorator computes them (a synthetic absolute base joined with the relative
argument), so equality is exact. `CountedMatchFiresOnNthOccurrence` (3rd open fires; 1st/2nd/4th
do not — one-shot), `MultipleRulesComposeIndependently` (two paths, two actions, independent
counters), `NonMatchingOperationsPassThrough` (unrelated ops mutate the overlay; the matching op
fires and leaves the overlay unmutated), and `EachFilesystemVerbCanFire` (every one of the seven
verbs fires on its own operation/target, confirming the operation-to-verb mapping). The test
holds the root as `std::shared_ptr<idirectory>` so the base-class convenience overloads
(`create_directory(path)`, `try_open_directory(path)`, …) are visible rather than hidden by the
decorator's virtual overrides.

## Known limitation — synthetic file flush/close serializes under `file_handle_mutex` (deferred)

Symptom to watch for: synthetic-file writes from the intercepted webcore engine appear to
serialize / stall under contention — many threads each closing or flushing a *different*
synthetic file handle make no progress in parallel, or `FlushFileBuffers` / `CloseHandle`
latency on synthetic handles grows with the number of concurrently-closing handles. Profiles
show threads blocked on `interception_context::file_handle_mutex` inside
`flush_file_handle` / `close_file_handle` in
`src/libraries/pil/src/intercepting/intercepting_webcore.h`.

Cause: the M-HWC-REVIEW2-4 fix made the synthetic-file **read** path two-phase — `read_file_handle`
snapshots the backing `shared_ptr<ifile>` + position under the lock, then runs `read_content`
**without** the lock held — so independent reads no longer serialize. The **write** side
(`flush_file_handle`, `close_file_handle`) was intentionally left holding `file_handle_mutex`
across the backing `write_content` call because that path also mutates / erases the map entry,
which is harder to make lock-free safely. This is a throughput concern, **not** a correctness
bug, and was deliberately deferred (per the request that surfaced it). No CHECKLIST item is
queued for it on purpose.

If this symptom is ever observed under real load, the fix mirrors the read path: snapshot the
`write_buffer` + backing `ifile` under the lock, drop the lock for `write_content`, then
re-acquire briefly to clear `dirty` (flush) or erase the entry (close), handling the case where
the entry was closed concurrently. At that point, file a CHECKLIST item and reference this note.
