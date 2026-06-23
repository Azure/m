# windows-win32-shim — design notes

Decisions are recorded as `SHIM-D<n>`. This crate is a parallel, all-Rust
reimplementation of the C++ `mwin32` DLL (`src/Windows/libraries/mwin32/`,
DESIGN-NOTES D1–D11). It is **not** layered on the C++ code; it calls through
the Rust `windows-platform-isolation` crate. Scope is currently **filesystem and
registry** only.

The C++ `mwin32` DESIGN-NOTES and tests are the behavioral spec for the ABI
surface; the Rust `windows-platform-isolation` crate is the implementation
substrate. Where the two disagree, this crate owns its own specified behavior
(see the repo's "Design Autonomy" rule) and chooses dependencies that satisfy it.

---

## SHIM-D1 — Parallel all-Rust shim, not layered on C++

The crate reimplements the `mwin32` ABI from scratch in Rust. It does not load,
link, or call the C++ `m_mwin32.dll`. The only things shared with the C++ side
are (a) the exported ABI shape (`mwin32.def` names/signatures) and (b) the
on-disk artifacts (`.pilcfg` JSON sidecar and the `<Platform>` XML saved state),
so the two implementations are interchangeable from a consumer's point of view.

## SHIM-D2 — `cdylib` exporting the `m`-prefixed Win32 C ABI; unsafe quarantined

The crate builds as a `cdylib` (plus `rlib` for in-crate integration tests) and
exports each entry point as `#[unsafe(no_mangle)] pub extern "system"`, matching
the undecorated names and `__stdcall`/`APIENTRY` calling convention the `.def`
expects. The C ABI inherently takes raw caller pointers, so this crate **cannot**
be `#![forbid(unsafe_code)]`. Instead, like `windows-threadpool`, the crate root
is `#![deny(unsafe_code)]` and only the ABI-boundary modules carry
`#[allow(unsafe_code)]`. All isolation logic stays in the safe
`windows-platform-isolation` crate, which remains `#![forbid(unsafe_code)]`.

## SHIM-D3 — Handle table with the C++ reserved-bit pattern (mwin32 D11 parity)

Minted `HANDLE`/`HKEY` values use the same reserved bit pattern as the C++ handle
table (mwin32 D11): bit 30 set, bits 31+ clear, low two bits clear, sequence in
the middle. This keeps minted handles unambiguous against real OS values and
predefined `HKEY` constants (`0x8000_000N`), and keeps them 32-bit-safe on 64-bit
Windows. The table interns isolation registry-key handles, file-handle state, and
find-enumeration state behind a `Mutex`; `deref`/`close` translate back.
Predefined `HKEY`s resolve (cached) to `windows-platform-isolation`
well-known roots.

## SHIM-D4 — Link-time Win32→`m` alias from the start

Unmodified clients that call genuine `RegOpenKeyExW` / `CreateFileW` are
redirected to the shim at link time by defining the `__imp_<Name>` IAT slots to
point at the `m<Name>` exports plus `/alternatename` fallbacks — the same
mechanism as the C++ `mwin32_alias` object (mwin32 D8). The export list is driven
by a `.def` generated/maintained alongside the Rust exports. `mCloseHandle` is
marked `noalias` (opt-in) so aliasing does not capture unrelated OS handles from
other code in the client.

**Rust realization (MW5).** Two checked-in manifests describe the one logical
alias roster, kept in lockstep by a unit test (`ndjson_aliased_set_matches_def_aliased_set`):

- `windows_win32_shim.def` drives [`alias_gen`](../src/alias_gen.rs), the Rust
  counterpart of `generate_mwin32_alias.cmake`. It parses the `.def` (skipping
  comments / `EXPORTS` / `noalias`, validating the `m([A-Z]|_)` shape, deduping)
  and emits C++ *text* — per aliased export `extern "C" void m<Name>();`, the
  decisive `extern "C" void (*__imp_<Name>)() = &m<Name>;` slot, and the
  `/alternatename:<Name>=m<Name>` pragma. This text is the source for the
  eventual C++ link-proof (MW5-6).
- `windows_win32_shim_aliases.ndjson` drives [`alias_obj`](../src/alias_obj.rs),
  which writes the alias **COFF object bytes directly** via the `object` crate —
  per aliased record an undefined external `m<Name>`, a defined public 8-byte
  `__imp_<Name>` slot in `.data` initialized by an `IMAGE_REL_AMD64_ADDR64`
  relocation to `&m<Name>`, and a `.drectve` section carrying the
  `/alternatename` fallbacks. The CLI [`gen-alias-obj`](../src/bin/gen-alias-obj.rs)
  exposes it (`--manifest` / `--out`, defaulting to the embedded manifest).

The decisive choice (SHIM-D4.1): **the production alias artifact is produced with
no C++ compiler and no MSVC tool.** We considered emitting C++/`.asm` and running
`cl.exe`/`ml64.exe`, and we checked for an `aliasobj.exe` next to `link.exe`
(it does not ship with MSVC). Writing the COFF directly is strictly better — it
is pure Rust, unit-testable by re-reading the emitted bytes, and even
cross-buildable — so only the client's own linker (unavoidable for any PE link)
consumes our output. NDJSON was chosen for the manifest specifically so the
versioned input supports comment / section lines and enable-by-uncommenting.
Both manifests are pure text/JSON processing (no `unsafe`, platform-independent)
and fully unit-tested here. The remaining work is the C++ link-proof EXE (MW5-6),
which is genuinely cross-toolchain and cannot run as a `cargo test`; it stays
deferred.

## SHIM-D5 — Reuse the C++ JSON `.pilcfg` sidecar format (artifact parity)

Configuration comes from a `<host-executable>.pilcfg` JSON sidecar with the same
schema and semantics as the C++ shim (`buffer_updates`, `record_modifications`,
`redirections`, `persisted_state`, `capture_snapshot`, `diagnostic_log`,
`fault_script`; the `webcore` block is ignored here). Loading is tolerant
(absent / unreadable / malformed → passthrough, never fails the host — mwin32
D5). This requires a JSON parser dependency. The set of `.pilcfg` features
actually honored is bounded by what `windows-platform-isolation` supports today;
unsupported keys are documented gaps rather than errors.

## SHIM-D6 — File content deferred (match C++ initial scope)

`ReadFile` / `WriteFile` / scatter-gather and other byte-content operations are
out of scope for the first cut, mirroring the C++ shim's deferral (mwin32 D14)
and the metadata-only `windows-platform-isolation` filesystem model. These
exports exist but return the Win32 not-supported failure shape
(`ERROR_NOT_SUPPORTED` / `FALSE` + `SetLastError`). Path metadata, attributes,
directory create/remove, handle minting, and directory enumeration are
implemented.

## SHIM-D7 — Error mapping owned here (Design Autonomy)

This crate owns the translation from `windows-platform-isolation`
`RegistryError` / `FilesystemError` into Win32 `LSTATUS` / `BOOL` +
`SetLastError`. `RegistryError::Os(u32)` / `FilesystemError::Os(u32)` carry the
raw Win32 code straight through; structured variants (`KeyNotFound`,
`ValueNotFound`, `NotFound`, `TypeMismatch`, …) map to the documented Win32 code
for that condition. The mapping table is specified here, not inherited from any
dependency.

## SHIM-D8 — Registry default backing is live passthrough

With no `.pilcfg` present, the registry surface defaults to live passthrough over
the real OS registry (`windows-platform-isolation` `LiveRegistry`), matching the
C++ shim's default. `.pilcfg` selects buffered / persisted-state / redirecting
layers on top. The filesystem surface defaults to live passthrough once the
`windows-platform-isolation` live FS provider (its M9) lands; until then the
filesystem surface is exercised against in-memory / artifact stacks.

## SHIM-D9 — `W` forms first, `A` forms later

The first milestones implement the wide (`*W`) entry points only. The ANSI
(`*A`) forms are a later milestone, implemented by transcoding through
`windows-text` code-page conversion (`CP_ACP`) at the boundary and delegating to
the `W` implementation.

## SHIM-D10 — Handles store an absolute `KeyPath`; ops resolve base + subkey

The C++ `mwin32` registry handle table holds live key *objects*. This crate
instead stores an absolute `windows-platform-isolation` `KeyPath` (interned in
the handle table) for each minted `HKEY`, and every operation resolves a *base*
path from the handle and *joins* it with the operation's subkey argument before
touching the substrate. Predefined `HKEY`s resolve to the canonical root path
(`KeyPath::parse(root.canonical_name())`); a minted `HKEY` derefs to its interned
`KeyPath`; anything else is `ERROR_INVALID_HANDLE`. `mRegOpenKeyExW` /
`mRegCreateKeyExW` with an empty subkey duplicate the base handle onto the same
key (a fresh minted `HKEY` over the same path), matching the Win32 contract.

This path-based model is chosen because the substrate is addressed by `KeyPath`
and has no persistent key-handle object; it keeps the handle table a pure
`usize`→payload map and makes every op a stateless `(base, subkey)` lookup. The
trade-off is that a handle does not pin a key open: a key deleted out from under
an open handle simply yields `ERROR_FILE_NOT_FOUND` on the next use, rather than
the Win32 "operations on the handle still succeed until close" behavior. That
divergence is acceptable for the shim's buffered/redirected use cases and is
recorded here as an owned decision.

## SHIM-D11 — `(REG_* type, raw bytes)` ↔ `ValueData` codec, with faithful round-trip caveats

`value_codec` owns the translation between the Win32 wire shape — a `u32`
`REG_*` type tag plus a raw little-endian byte buffer — and the substrate's
typed `ValueData`. The mapping is specified here (Design Autonomy), not inherited
from `windows-sys`:

- `REG_SZ` / `REG_EXPAND_SZ` ↔ `ValueData::String` / `ExpandString`: the byte
  buffer is interpreted as UTF-16 code units verbatim. The encoder emits exactly
  the units the caller stored; **no NUL terminator is added or stripped**, so a
  trailing-NUL buffer round-trips with its byte count preserved. An **odd-length**
  string buffer is `ERROR_INVALID_DATA`.
- `REG_MULTI_SZ` ↔ `ValueData::MultiString`: units are split on NUL; trailing
  empty segments (the customary double-NUL terminator) are dropped via
  `take_while` over non-empty runs, and an empty list round-trips to an empty
  buffer.
- `REG_DWORD` ↔ `ValueData::Dword` requires **exactly 4** bytes; `REG_QWORD` ↔
  `ValueData::Qword` requires **exactly 8** bytes (little-endian). Any other
  length is `ERROR_INVALID_DATA`.
- `REG_BINARY` ↔ `ValueData::Binary`: bytes pass through verbatim.

Two deliberate degradations keep the codec total against the substrate's
`#[non_exhaustive] ValueData` and against unknown wire tags: encoding a
`ValueData` variant this layer does not model falls back to `(REG_BINARY, &[])`,
and decoding an unrecognized `REG_*` type yields `ValueData::Binary(bytes)`
rather than an error. Both are recorded as owned choices so a future variant or
type tag fails soft (as opaque binary) instead of panicking.

## SHIM-D12 — Filesystem handle model, attribute↔metadata translation, and accept-and-ignore mutation

The MW3 filesystem C ABI (`m*W` entry points in `mwinfile`) delegates to a safe,
surface-generic core (`fs_ops`) over the session's live `Filesystem` facade,
exactly as the registry surface delegates to `reg_ops` (SHIM-D2 / SHIM-D10). Four
owned decisions shape it:

- **Handle model.** A minted file `HANDLE` (`HandlePayload::File`) stores the
  public `FilePath` the caller opened plus a sequential byte `position`; a minted
  find `HANDLE` (`HandlePayload::Find`) stores a captured `DirEntry` listing plus
  a cursor. The facade is path-addressed and has no persistent file-handle
  object, so — as with SHIM-D10 — a handle does not pin a node open: a node
  deleted out from under an open handle yields `ERROR_FILE_NOT_FOUND` on next
  use. `mCloseHandle` is the one entry point that sees *all* `CloseHandle`
  traffic (unlike `mRegCloseKey`): a non-minted value is forwarded to the real OS
  `CloseHandle`, a minted value is released from the table.

- **Attribute ↔ `FileMetadata` translation.** `to_win32_attributes` projects the
  surface's `FileMetadata.attributes` (already Win32 `FILE_ATTRIBUTE_*` verbatim)
  plus its `NodeKind` onto the Win32 attribute DWORD: the directory bit is forced
  to match the kind, and an otherwise-empty mask collapses to
  `FILE_ATTRIBUTE_NORMAL` (Win32 never reports `0`). The facade's `metadata`
  reads **files only**, so a directory's metadata is recovered from its parent
  listing (`stat_path` → `directory_metadata`); a parentless root (e.g. `C:\`) or
  a leaf the listing does not surface by exact name synthesizes an empty
  directory carrying just `FILE_ATTRIBUTE_DIRECTORY`. `FILETIME`s and the
  high/low size split are filled by the ABI layer from the `i64` tick / `u64`
  size fields.

- **Attribute mutation is accept-and-ignore.** `mSetFileAttributesW` validates
  the node exists and then reports success **without persisting** any change.
  This matches the C++ shim (its metadata is read-only) and is *required* for
  safety here: the live provider has no attribute-only write, and routing through
  `write_file` would truncate real file content (the live `FileHandle::create`
  recreates the file). The specified behavior is therefore "validate + succeed",
  and the dependency is used only to validate existence.

- **Content and move/copy deferral.** Per SHIM-D6, byte content is out of MW3
  scope: `mReadFile`, `mWriteFile`, `mReadFileScatter` / `mWriteFileGather`,
  `mMoveFileExW`, and `mCopyFileExW` exist for ABI completeness but report the
  Win32 not-supported shape (`SetLastError(ERROR_NOT_SUPPORTED)` + `FALSE`);
  `mMoveFileExW` additionally awaits a future isolation rename verb. Consequently
  a file size is always its metadata size and `TRUNCATE_EXISTING` opens without
  truncating.

One further owned simplification: a find enumeration captures every child of the
pattern's parent directory in ordinal order, then **applies the pattern's leaf as
a search filter** (MW8 / SHIM-D14). `mFindFirstFileW` filters with a
case-insensitive name match over the leaf using Win32 DOS-wildcard semantics; the
matcher itself is `windows-text::name_matches_expression` (WT-6), chosen because
its behavior matches the Win32 `FsRtlIsNameInExpression` semantics this shim
specifies (Design Autonomy). A rootless single component (no parent) is
`ERROR_INVALID_PARAMETER`; a listing with no matching entry is
`ERROR_FILE_NOT_FOUND`. The captured `FindEnumerationState` carries the
`SearchPredicate`, so `mFindNextFileW` keeps the same filter across the whole
enumeration. The extended search operations and info levels layer on top of this
in SHIM-D14.

## SHIM-D13 — `.pilcfg`-driven session composition (registry backing, capture)

The MW4 `.pilcfg` sidecar (SHIM-D5) selects how the process-wide `session`
composes its isolation stack. The schema parse and the stack composition are two
separately owned concerns:

- **Parse contract (`pilcfg`).** `parse_pilcfg` is **strict** — a non-object
  root, a recognized member of the wrong JSON type, or a malformed `redirections`
  entry is a `PilcfgError`. `load_pilcfg` (the sidecar entry point) is
  **tolerant** — an absent / unreadable / malformed `<current_exe>.pilcfg`
  collapses to `Pilcfg::default()` (all-passthrough), never failing the host
  (mwin32 D5). The recognized members are exactly the SHIM-D5 set; `webcore` and
  any unknown member are ignored. `%VAR%` expansion (`expand_environment_path`)
  is applied **only** to path-valued members (`persisted_state`,
  `capture_snapshot`, `diagnostic_log`, `fault_script`), matching the C++ shim —
  never to redirection keys. An undefined `%TOKEN%` is left verbatim (delimiters
  included), as is a trailing unmatched `%`. The JSON parser is the pure-Rust,
  zero-dependency, `forbid(unsafe_code)`-friendly `tinyjson`; per Design Autonomy
  the schema↔value mapping is owned here, and tinyjson is merely the byte→AST
  reader.

- **Registry backing composition (`session`).** `ShimSession::from_config`
  selects one concrete `RegistryBacking` (a local enum that is itself a
  `Surface`, so `reg_ops` and the handle table stay surface-generic with no
  dynamic dispatch): a non-empty `persisted_state` that **loads** runs entirely
  against the in-memory snapshot (`RegistryBacking::Persisted`, and the layer
  flags are ignored, as in the C++ `build_platform_from_config` mode (c)); else
  `buffer_updates` interposes a write-buffering layer over the live registry
  (`RegistryBacking::Buffered`); else live passthrough (`RegistryBacking::Live`,
  SHIM-D8). A present-but-unreadable / malformed `persisted_state` falls back to
  live passthrough rather than failing the host — an owned tolerance choice
  consistent with SHIM-D5 (the C++ ctor would throw; we do not).

- **`capture_snapshot` on teardown.** `ShimSession::capture_snapshot` is
  best-effort and explicit: with no configured path it is a no-op; with a
  persisted backing it folds the live `OverlayTree` (base snapshot plus the
  session's overlay writes) into a flat `Hive` and serializes it via
  `save_registry_hive` to the configured artifact, round-tripping with
  `load_registry_hive`. Because `save_registry_hive` takes a `Hive` (not an
  `OverlayTree`), the fold (`fold_tree_to_hive`) re-materializes the tree's
  enumerated keys and values; this is the shim's owned bridge, not a facade API.

**Documented gaps for this milestone** (recorded so the absence is intentional,
not an oversight — each is queued as a later MW item where applicable):

- `record_modifications`, `redirections`, `diagnostic_log`, and `fault_script`
  are parsed and preserved but **not yet honored**: `windows-platform-isolation`
  exposes no journaling-to-file, key-redirection, or fault-injection decorator to
  route them through. They are gaps, not errors.
- The **filesystem** surface is always live passthrough; there is no buffered or
  persisted filesystem decorator, so `persisted_state` / `buffer_updates` affect
  the **registry only**.
- A live or buffered registry backing **cannot** be serialized, so
  `capture_snapshot` returns `false` for those backings (only a persisted backing
  is captured).
- Capture is invoked **explicitly**, not automatically at process exit: Rust
  `static`s do not run `Drop`, so the at-exit (`DllMain` `DLL_PROCESS_DETACH`)
  wiring the C++ destructor relies on is deferred.


## SHIM-D14 — `FindFirstFileEx` family: search-op / info-level / flag mapping and 8.3 short-name passthrough

MW8 completes the `FindFirstFile*` family on top of the SHIM-D12 enumeration
model. `mFindFirstFileExW`, `mFindFirstFileTransactedW`, and the plain
`mFindFirstFileW` share one engine (`fs_ops::find_first` + `find_next`); only the
extended call validates and maps the additional parameters. The mapping is owned
behavior (Design Autonomy): the shim **specifies** which info levels, search
operations, and flag bits it accepts, and `windows-sys`'s named `FINDEX_*` /
`FIND_FIRST_EX_*` constants are compared by value to satisfy that specification
(never matched as patterns — they are camelCase values, not Rust enum variants).

- **Info level → short-name emission.** `FindExInfoStandard` emits the 8.3 short
  name; `FindExInfoBasic` suppresses it (the documented Basic optimization). Any
  other level (e.g. `FindExInfoMaxInfoLevel`) is `ERROR_INVALID_PARAMETER`. The
  decision is captured once per enumeration as
  `FindEnumerationState.emit_short_name` and returned from `find_next`, so the
  whole `FindNextFileW` walk honors the level chosen at `FindFirstFileEx` time.

- **Search operation → predicate.** `FindExSearchNameMatch` →
  `SearchOp::NameMatch`; `FindExSearchLimitToDirectories` →
  `SearchOp::LimitToDirectories` (the leaf filter additionally requires a
  directory entry). `FindExSearchLimitToDevices`, `FindExSearchMaxSearchOp`, and
  any other value are `ERROR_INVALID_PARAMETER`.

- **Additional flags.** `FIND_FIRST_EX_CASE_SENSITIVE` makes the leaf match
  case-sensitive (otherwise the Win32-default case-insensitive match of
  SHIM-D12). `FIND_FIRST_EX_LARGE_FETCH` and `FIND_FIRST_EX_ON_DISK_ENTRIES_ONLY`
  are accepted and ignored (performance/locality hints with no observable effect
  on the in-memory enumeration). Any unrecognized bit is
  `ERROR_INVALID_PARAMETER`.

- **`lpSearchFilter`.** Reserved by Win32; must be null, else
  `ERROR_INVALID_PARAMETER`.

- **Short-name source.** The 8.3 alternate name copied into `cAlternateFileName`
  is `DirEntry.short_name` supplied by the isolation surface (isolation M10); the
  shim does not synthesize short names. When the surface has no short name for an
  entry, `cAlternateFileName` is left empty.

- **Transacted stub.** `mFindFirstFileTransactedW` ignores its trailing
  transaction handle and forwards to `mFindFirstFileExW` — the shim has no
  transaction surface, matching the C++ forwarding stub.
