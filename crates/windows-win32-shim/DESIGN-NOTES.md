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

**Scope.** The original surface is **filesystem and registry** (the C++ `mwin32`
ABI). SHIM-D16/SHIM-D17 extend the crate with two **new** surfaces that have no
C++ `mwin32` antecedent — the **dynamic-loader** shims (`mLoadLibrary*` /
`mGetProcAddress`) and the **COM activation** shims (`mCoCreateInstance` / …) —
realizing platform-isolation decisions **D26** (loader shims) and **D29**
(observe unaddressed seams) under the same aliasobj technique (**D24**) and
off/record/replay modes (**D25**) the registry/filesystem surfaces already use.

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
and fully unit-tested here.

**Link-proof (MW5-6).** The cross-toolchain verification reuses the mwin32 C++
tree's link-proof program (`test/test_mwin32_alias.cpp`): `linkproof/linkproof_main.cpp`
is that program with its GoogleTest harness swapped for a dependency-free `main`,
and `linkproof/run-linkproof.ps1` drives the production link recipe end to end —
`cargo build` the cdylib, `gen-alias-obj` emits the alias COFF, `cl /c` the
genuine-`<windows.h>` TU, `link` it with the alias `.obj` + the cdylib import
library `windows_win32_shim.dll.lib`, then run under a buffered `.pilcfg`.
`dumpbin /imports` confirms the EXE binds `mRegCreateKeyExW` / `mRegSetValueExW`
/ `mRegQueryValueExW` / `mRegCloseKey` from `windows_win32_shim.dll` (the
client's `Reg*` calls are redirected into the shim, not advapi32), and the
runtime exit code flips with shim config (buffered ⇒ overlay captures the write,
the live-registry negative check passes, exit 0; no `.pilcfg` / live passthrough
⇒ the negative check finds the key, exit 1). Note the Rust exports are
`extern "system"` + `#[unsafe(no_mangle)]` (undecorated), so the cdylib's own
import library resolves the `m<Name>` targets — the separate undecorated import
library the C++ build needed (its `mReg*` had C++ linkage) is unnecessary here.

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

- **Filesystem backing composition (`session`).** `ShimSession::from_config`
  selects one concrete `FilesystemBacking` (a local enum that is itself an
  `FsSurface`, so `fs_ops` and the handle table stay surface-generic with no
  dynamic dispatch): `buffer_updates` interposes an overlay-over-live write
  buffer (`FsBuffered`, platform-isolation D30) so an unmodified consumer's
  namespace mutations land in the in-memory overlay and the live filesystem is
  left untouched until an explicit commit; else live passthrough
  (`FilesystemBacking::Live`). `buffer_updates` thus now buffers **both** the
  registry and the filesystem with a single sidecar flag, matching the C++
  shim's single-knob intent. The large `Buffered` variant is boxed so the enum
  is not dominated by the overlay/journal footprint of the buffered case.

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
- The **filesystem** surface honors `buffer_updates` (overlay-over-live
  `FsBuffered`, platform-isolation D30) but not `persisted_state`: there is no
  persisted or capturable filesystem snapshot yet, so a buffered filesystem's
  overlay is discarded at teardown (no filesystem analogue of
  `capture_snapshot`). `persisted_state` therefore affects the **registry only**.
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

## SHIM-D15 — ANSI (`A`) forms: `CP_ACP` boundary transcoding over the shared cores (MW6)

MW6 realizes the SHIM-D9 deferral. The `A` entry points are thin boundary
adapters: they transcode their string arguments between `CP_ACP` and UTF-16 and
then delegate to the **same** safe `reg_ops` / `fs_ops` cores the `W` exports
call — never to the `W` C-ABI export. (SHIM-D9 said "delegating to the `W`
implementation"; the implementation *is* the shared core, so `A` and `W` are
peers over one core rather than `A` calling `W`.) All conversion goes through the
`windows-text` `CP_ACP` code page (`Utf16::from_code_page` / `to_code_page`),
which is the single owned transcoding dependency.

- **Scope (owned).** MW6 adds `A` forms only for the **functionally implemented**
  `W` cores — the set with observable, testable `A`/`W` parity (open/create/delete
  keys, value set/query/get/delete/enum, key enum/info on the registry side;
  create/delete/attributes/dir/find on the filesystem side). `A` spellings whose
  `W` counterpart is a `NOT_SUPPORTED` stub are deliberately **not** exported:
  delegating to a stub yields nothing to mirror, and adding them would balloon the
  surface with no behavior. They remain commented in `windows_win32_shim.def` /
  `windows_win32_shim_aliases.ndjson` until their `W` core lands (the same
  incremental enable-by-uncommenting the manifests already use).

- **`ansi` helper module.** The pointer-free transcoding primitives live in
  `src/ansi.rs` (no exports, fully unit-tested): `ansi_to_utf16` (a
  NUL-terminated `LPCSTR` → `Utf16`), `fill_ansi_fixed` (a `Utf16` into a fixed
  `CHAR` array, truncating with a guaranteed NUL — the `WIN32_FIND_DATAA`
  `cFileName` / `cAlternateFileName` shape), `write_ansi_name` (a `Utf16` into a
  caller `LPSTR` honoring the `lpcch` character in/out contract — `RegEnumKeyExA`
  / `RegEnumValueA`), and the value-DATA converters `data_wide_to_ansi` /
  `data_ansi_to_wide`.

- **Registry string-DATA conversion.** For the textual value types (`REG_SZ`,
  `REG_EXPAND_SZ`, `REG_LINK`, `REG_MULTI_SZ`) the `A` value entry points convert
  the value **data** between `CP_ACP` and the UTF-16 stored form, so an `A` writer
  and a `W` reader (or vice-versa) agree (matching C++ `mwin32` D6). The whole
  buffer is converted in one call — the `windows-text` wrappers pass the slice by
  length (not NUL-terminated), so embedded and trailing NULs are preserved and
  `REG_MULTI_SZ` is handled uniformly with the single-string types. Non-string
  types carry their bytes through unchanged, and `A` query sizes (`lpcbData`) are
  reported in ANSI bytes.

- **Owned simplifications.** A `CP_ACP` decode failure (which the ANSI code page
  does not normally produce) yields an empty string, so the downstream op fails
  with the same not-found / invalid shape the `W` form would for an empty path.
  `mRegQueryInfoKeyA` reports its max-length fields in stored-form units rather
  than re-measuring ANSI byte lengths — exact for ASCII, a recorded divergence
  acceptable for the shim's uses.

## SHIM-D16 — Dynamic-loader shims (`mLoadLibrary*` / `mGetProcAddress` / module handles)

The loader family is the mechanism (platform-isolation **D26**) by which
**runtime-bound** symbols and **substitutable engines** come under the same
redirection as static imports. A dynamically-resolved API is called through a
function pointer the host obtained at runtime, **not** through its import table,
so aliasobj (SHIM-D4 / D24) cannot reach it directly; intercepting the host's
*loader calls* is the only link-time seam that does. Exported set:

- `mLoadLibraryW` / `mLoadLibraryA`, `mLoadLibraryExW` / `mLoadLibraryExA`
- `mGetProcAddress`
- `mFreeLibrary`
- `mGetModuleHandleW` / `mGetModuleHandleA`, `mGetModuleHandleExW` / `mGetModuleHandleExA`

**Behavior model**, driven by the session mode (SHIM-D13 / D25), with a module
handle table that is a peer of the existing handle table (SHIM-D3) interning each
load as either a **real** `HMODULE` (passthrough) or a **minted sentinel**
`HMODULE` standing for a shim-substituted module:

- **off / passthrough.** `mLoadLibrary*` forwards to the real loader and returns
  the real `HMODULE`; `mGetProcAddress` forwards to the real `GetProcAddress`.
  The only addition over the bare OS call is the observation hook below — the call
  is otherwise transparent (the D24 identity requirement).
- **observe (D29).** Every `LoadLibrary(name)` and every resolved
  `(module-name, proc-name-or-ordinal)` is reported to the session's observation
  sink, keyed so a known-safe `(api, target)` pair can later be suppressed by the
  volume policy. Observation never changes the returned value.
- **substitute (record/replay, opt-in per target).** Two distinct substitutions:
  1. **Proc redirection.** `mGetProcAddress(h, name)` consults a
     **name→shim-proc table** (the same `m*` bodies the static aliases resolve
     to). When `name` is a shimmed API and the mode is not off, it returns the
     address of the shim's own export instead of the real proc, so a
     *dynamically*-resolved call lands in the same body as a *statically*-aliased
     one. The table is seeded from the current export roster and **grows as new
     surfaces (network, COM) land** — the mechanism ships even though most
     interesting targets arrive later.
  2. **Engine substitution.** `mLoadLibrary*` of a registered engine DLL name
     returns a minted sentinel `HMODULE`; `mGetProcAddress` against that sentinel
     returns shim-supplied entry points (e.g. a fake `WebCoreActivate`). This is
     the "become the engine" route (D26): the host's loader call is the seam, not
     the engine's internals.

**Handle accounting.** `mFreeLibrary` releases a minted sentinel from the table
(no OS call) or forwards a real `HMODULE` to the real `FreeLibrary`.
`mGetModuleHandle*` resolves a name to a previously-minted sentinel when one
exists (so a substituted engine is found again), else forwards to the OS;
`GetModuleHandleEx`'s pin / ref-count flags are honored for real modules and
modeled minimally for sentinels.

**Aliasing posture (non-opt-in).** The entire loader family — `mLoadLibrary*`,
`mGetProcAddress`, `mFreeLibrary`, and `mGetModuleHandle*` — is **non-opt-in**:
every one is carried in the dual manifests with a `/alternatename` entry, so the
host's naive `LoadLibraryW` / `GetProcAddress` / `FreeLibrary` / `GetModuleHandle`
imports are rewritten to the `m*` body at link time with no per-call gate. This
is the **opposite** of `mCloseHandle` (SHIM-D4, `noalias`/opt-in): universal
capture is the entire point of D26, because a runtime-bound call can only be
reached if the loader call that produced its function pointer was itself
captured. The safety net is therefore **not** opt-out but an invariant: every
loader export stays byte-for-byte **transparent for any `HMODULE` / `FARPROC` it
did not mint** (and fully transparent in off-mode), so unrelated host loads are
unaffected even though they all pass through the shim.

**Ownership / safety.** The raw `HMODULE` / `FARPROC` / function-pointer
manipulation is Windows ABI glue confined to the shim's `#[allow(unsafe_code)]`
boundary module (SHIM-D2); the **policy** (mode, the engine-name and proc-name
tables, the observation sink) is safe session state, composed exactly as the
registry/filesystem backings are (SHIM-D13).

**Owned simplification (first cut).** The name→shim-proc table, the
engine-substitution registry, and the observation sink are **shim-local** data,
seeded programmatically and (later) from `.pilcfg`; promoting them into a shared
`windows-platform-isolation` "loader surface" is deferred until a second consumer
needs it ("design notes are not a work queue").

## SHIM-D17 — COM activation shims (`mCoCreateInstance` / `mCoGetClassObject` / …)

`CoCreateInstance` is an **ole32 import**, so unlike a vtable method it is
directly aliasable (D24) — and it is **not** reachable through the loader shims
(it does not go through `GetProcAddress`), which is why COM needs its own exports.
Providing for the loader without COM would leave object activation as a blind
spot. Exported set (first cut):

- `mCoCreateInstance`, `mCoCreateInstanceEx`
- `mCoGetClassObject`
- passthrough lifecycle: `mCoInitialize` / `mCoInitializeEx` / `mCoUninitialize`
  (forwarded; present so the COM apartment lifecycle is observable and the alias
  roster is coherent)

**Behavior model**, mirroring SHIM-D16 and driven by session mode:

- **off / passthrough.** Forward to the real ole32 entry point; return the real
  interface pointer unchanged.
- **observe (D29).** Report `(CLSID, IID, CLSCTX)` activations to the observation
  sink. This is a high-traffic surface, so the volume policy's known-safe
  allowlist is expected to matter here in particular.
- **substitute (replay / selective record).** Consult a
  **CLSID→shim-class-factory** registry. When a factory is registered for the
  requested CLSID and the mode allows, the shim's factory produces an object
  implementing the requested IID — a shim-supplied COM object (e.g. a faked
  config admin manager so dev replay needs no real engine). With no factory
  registered, forward to the real activation (or fail per a configured policy).

**COM plumbing.** Vending a substitute object needs minimal `IUnknown` /
`IClassFactory` vtable support. The decision is to implement these as raw
`windows-sys` vtable structs in the shim's `#[allow(unsafe_code)]` boundary
module rather than pulling the heavier `windows` COM macro stack into this crate,
keeping the dependency story uniform with the rest of the shim (Design Autonomy);
revisit if substitute objects become numerous. **Record-mode method-level
journaling** — a forwarding wrapper that logs each vtable call on a *real* object
— is **out of first-cut scope**: it is per-interface work with no generic form,
so the first cut does activation-observation + substitution only, and per-method
wrapping is added for a specific interface when a scenario needs it.

**Ownership / safety.** As with SHIM-D16, the GUID / HRESULT / vtable glue is
shim-local unsafe ABI; the CLSID registry and observation sink are safe session
state.

**Aliasing posture (non-opt-in).** Every COM export — `mCoCreateInstance`,
`mCoCreateInstanceEx`, `mCoGetClassObject`, and the passthrough lifecycle
exports — is **non-opt-in**, manifest'd with a `/alternatename` entry like the
loader family, so all client COM activation in a relinked module is captured at
link time. Unlike `mCloseHandle`, none of these is `noalias`. Because capture is
total and `CoCreateInstance` is high-traffic, the runtime cost is bounded by the
transparency invariant (forward unchanged when no factory is registered and in
off-mode) and by the D29 volume policy suppressing known-safe `(CLSID, IID)`
activations from the observation log.

## SHIM-D18 — In-process web-host response-path module (loadable module + handler bridge)

Realizes the **in-process** replacement for platform-isolation **D17**'s
out-of-process HWC pipeline. Rather than a separate service + named-pipe
protocol, the shim loads into the web host — it is already a load-time dependency
via the aliasobj relink (MW5) — and inserts a request handler at the host's
**public activation seam**: an IIS/HWC native module (`RegisterModule` →
`IHttpModuleRegistrationInfo::SetRequestNotifications` → `CHttpModule`) and/or a
handler-factory acquisition import that the alias technique (D24) redirects.
Engine activation reached through the loader shims (SHIM-D16 / D26) is the same
seam family. Only public Windows SDK names are used.

The handler the shim hands back bridges the host's per-request calls into the
**safe** `RequestHandler` surface owned by `windows-platform-isolation` (M8): the
unsafe vtable / `IHttpContext` glue lives in the shim's `#[allow(unsafe_code)]`
boundary module (SHIM-D2); the policy (identity / journaling / future
substituting decorator, selected by session mode, D25) is safe platform-isolation
state. The **first cut wires the identity decorator** — code runs on the response
path with **zero behavior change** — establishing the seam before any redirection
is applied.

Per-interface method-level journaling beyond the begin/send-response notification
points is added per interface as scenarios need it, not generically (mirrors
SHIM-D17's COM stance). The out-of-process service variant remains a deferred
optimization owned by platform-isolation D17, not this crate.

### Chosen activation seam (MW11)

The seam is the IIS native-module registration path, expressed entirely in
public Windows SDK names:

```
RegisterModule(version, IHttpModuleRegistrationInfo*, IHttpServer*)
  └─ IHttpModuleRegistrationInfo::SetRequestNotifications(IHttpModuleFactory*, …)
       └─ IHttpModuleFactory::GetHttpModule(CHttpModule**, IModuleAllocator*)
            └─ CHttpModule::OnBeginRequest / OnSendResponse  →  per-request seam
```

The shim exports `mRegisterModule`; the relinked host's `RegisterModule` entry is
redirected to it via the alias technique (D24), so the host hands the shim its
`IHttpModuleRegistrationInfo`. `mRegisterModule` mints a shim `IHttpModuleFactory`
and registers it for the begin-request and send-response notifications; the
factory's `GetHttpModule` vends a shim `CHttpModule` whose notification methods
are the per-request bridge (the safe-surface translation lands in MW12). The HWC
host-activation API (`WebCoreActivate` / `WebCoreSetMetadata` / `WebCoreShutdown`)
stays out of scope (SHIM-D5); it remains as commented parity placeholders in the
manifests.

**Modeled-subset vtables.** `windows-sys` does not surface the IIS hosting ABI
(`httpserv.h`), so the three module vtables are hand-rolled `#[repr(C)]` structs
(Design Autonomy), each a **documented subset** of its real interface — the slots
the shim invokes or vends, not the full notification roster:

- `IHttpModuleRegistrationInfo` (host-provided): the shim calls only
  `SetRequestNotifications`; the preceding/following slots are modeled so the
  call lands at the right vtable offset.
- `IHttpModuleFactory` (shim-vended): `GetHttpModule` + `Terminate`.
- `CHttpModule` (shim-vended): `OnBeginRequest` + `OnSendResponse` + `Dispose`.

The precise real-IIS vtable layout is pinned when a real host is bound; until
then the emulated-host harness (MW11-5) builds the same structs, so both sides
agree. Additional notification slots are added per scenario, never generically
(same stance as the per-interface journaling note above).

**Pass-through first cut (MW11).** The shim `CHttpModule` returns
`RQ_NOTIFICATION_CONTINUE` from every notification — the host pipeline proceeds
unchanged — while recording the call through the session's web policy
([`crate::web`]). This is the "code on the response path, zero behavior change"
endpoint; the disposition is fixed to *continue* and the request/response are
never inspected. Mode gates only observation (`Off` = silent identity, `Observe`
= recorded identity), exactly as the COM family gates substitution/observation
(SHIM-D17). Installation is unconditional in both modes — being resident on the
path is the point; the mode only decides whether the traversal is journaled.

**Non-opt-in aliasing.** `mRegisterModule` joins the loader/COM families as a
non-opt-in alias (`/alternatename`, never `noalias`): redirecting the host's
module-registration entry is the whole mechanism by which the shim reaches the
response path. The residency probe `mShimWebProbe` (MW11-1) is shim-internal —
no Win32 antecedent — so it is exported but absent from the alias roster.

## SHIM-D19 — `wordy`: a shim-unaware HWC proof application + isolation harness

The registry / filesystem / loader / COM / web seams (SHIM-D2..D18) need a
*realistic, third-party-shaped* HWC application to prove that the link-time alias
(SHIM-D4 / D24) redirects an **ordinary** native module's host calls into the
isolation overlay — and, longer term, to drive that application's business logic
on an unprivileged developer machine with **no HWC installed**. `wordy`
(`crates/wordy`) is that application: a Rust IIS native-module REST "shared
dictionary" service. It is both the proof vehicle for the whole isolation surface
and a reusable testing asset for other HWC users in the division, so it is
deliberately **generic** — organized around the host seams *any* HWC app touches,
not shaped to mirror any one client (e.g. a specific relay / host-agent service).

**Shim-unaware contract.** `wordy`'s source carries zero isolation awareness — no
dependency on this crate, no feature flags, no capture hooks. The only
isolation-related artifacts are *external to its code*: (1) the alias `.obj` +
shim import lib, injected into the link by a **generic, env-driven `build.rs`**
that adds an extra object + library search path only when told to, and otherwise
builds a plain binary (that plain build is the proof the crate is unaware); and
(2) a `.pilcfg` sidecar beside the binary. Isolation is an act performed *on*
`wordy` from the outside — exactly the posture a real third-party app is in (we
do not get to edit their `build.rs` either). The `build.rs` holds no isolation
knowledge; it is "link whatever I am handed" glue, chosen over external
`RUSTFLAGS` / `.cargo/config.toml` injection for build-cache reliability (the
latter leaks to every crate in the graph and invalidates caches on toggle).

**Host-seam coverage (the point).** `wordy` exercises the seams that matter, each
mapping to an isolation surface: inbound HTTP request **read** and response
**write** (IIS native-module vtables — `wordy` hand-declares its **own** modeled
vtable subset, a peer of `mwinweb` but living in `wordy`, to stay shim-unaware);
filesystem **namespace / metadata** ops; and **asynchronous request completion on
the Windows thread pool**. The async path (every route) is genuine: the heavy
dictionary work — anagram search, batch spell-check, `fst` edit-distance
suggestions, `regex` enumeration over a ~60k-word list — is offloaded via
`windows-threadpool::submit_once`; the module returns `RQ_NOTIFICATION_PENDING`
and the pool work item writes the response and calls `IHttpContext::PostCompletion`.
This forces the redirection open across a **second seam beyond the filesystem**
(the thread-pool / loader surface), guarding against an accidentally FS-only
proof.

**Custom dictionary = namespace-only store (SHIM-D6 alignment).** The mutable,
per-user / per-locale "custom dictionary" stores each word as an **empty file
whose name encodes the word**, under `{dynamic-root}/{locale}/{user}/`. Add /
remove / exists / enumerate are therefore `CreateFileW` / `DeleteFileW` /
`GetFileAttributesW` / `FindFirst`+`FindNext` — purely the metadata / namespace
surface this crate already isolates; **no file content is needed**, so the design
sits entirely inside SHIM-D6's content deferral. The read-only **shared
dictionary** is a static word-list file read into memory (a content read, which
is legitimately *not* isolated). The two roots thus split exactly along what the
shim isolates today. Word ↔ filename uses a reversible, path-escape-proof
encoding (lowercase + percent-encode anything outside `[a-z]`) so hostile input
cannot escape the dictionary directory.

**Forward-compatible identity / locale.** The service has no per-user logon
today, but is **designed and coded as if per-user**: a `Principal` / `UserId`
newtype is threaded through every handler and resolved from a request header,
defaulting to a single built-in user when absent. This is the relay-service-style
"the app reads its own claims" posture — and no real authentication is
implemented, consistent with the finding that HWC apps perform their own
payload-level auth while the platform hands them anonymous requests. A `Locale`
enum (only `en-US` populated now) namespaces storage so other locales drop in
without a schema change.

**Word list.** A SCOWL / ESDB-generated `en-US` list (offensive category
excluded, moderate size) is vendored as the shared dictionary, with its license
file (public-domain core); chosen for tunable size, built-in profanity exclusion,
and native dialect modeling that seeds the locale carve-out.

**Deferred (not scheduled now).** A neutral `iis-native-module` crate shared by
`wordy` and `mwinweb` (to remove the modeled-vtable duplication) is a known
option, explicitly **not** queued — `wordy` carries its own subset until the
duplication proves painful. The `windows-threadpool-executor` `async` / `await`
variant of the async path is a follow-on to the `submit_once` first cut.

**Isolation proof — closed end to end (MW15).** The redirection is now proven at
both layers, against the unmodified `wordy`:

- **Link time (static).** `hwcproof/build-aliased-wordy.ps1` builds `wordy` with
  the alias object + shim import library injected through the generic `build.rs`
  vars and asserts, via `dumpbin /imports wordy.dll`, that every aliased
  filesystem entry point (plus the loader / registry / COM manifest) binds
  `windows_win32_shim.dll` and that **zero** aliased FS names survive as
  `kernel32` imports.
- **Run time (dynamic).** `hwcproof/run-hwcproof.ps1` genuinely `WebCoreActivate`s
  HWC with a buffered host sidecar and drives POST/GET/DELETE `/custom/widget`
  over real HTTP. The `isolated` variant proves the add/get/delete round-trip
  succeeds (read-your-writes) while the live custom root is **never created on
  disk** — the module's namespace mutations stayed in the shim overlay
  (SHIM-D13 / platform-isolation D30). The `native` variant is the negative
  control: the same round-trip with an un-aliased `wordy.dll` **does** create the
  live root, proving the overlay is a real effect and the assertion discriminates.
- **Gate.** `tests/hwc_isolation.rs` wraps the `isolated` run as an `#[ignore]`d
  integration test that skips cleanly when HWC is absent or the URL is unbindable
  (the proof needs the HWC feature and a URL reservation), so the default test
  run never depends on host configuration.

**Decision (MW15): isolate the loadable module, not the host.** The alias object
is injected only into the **cdylib** (`build.rs` uses `rustc-link-arg-cdylib`),
leaving any host binary that loads it an ordinary executable. An object file is
linked unconditionally, so aliasing the host too would route the host's own
config-file writes through the buffered shim and break activation. Scoping the
alias to the module mirrors production exactly: a native HWC worker hosting an
isolated third-party module.

Realized by **MW13** (synchronous service), **MW14** (asynchronous completion on
the thread pool), and **MW15** (isolation proof — link-time `dumpbin` + genuine-HWC
runtime harness with a native negative control). See CHECKLIST.md.

**MW18 amendment — `wordy` split for the egress validation tier (SHIM-D23).** The
per-user custom dictionary's *storage* is carved out of `wordy` into the `merriam`
service; `wordy` keeps its shared-dictionary CPU work and **relays** the custom-
dict operations to `merriam` over **ordinary WinHTTP** (`wordy/src/winhttp.rs` +
`relay.rs`, WD-D13). This is the outbound counterpart of the inbound IIS seam:
`wordy` stays shim-unaware, so when relinked against the alias object its `WinHttp*`
imports are rerouted into the egress seam (MW17) with no `wordy` change. The route
handlers now depend on a `CustomStore` trait; the filesystem store (`custom.rs`)
is **re-homed** as one backing (not deleted) and selected when `WORDY_CUSTOM_ROOT`
is set, so the MW15 filesystem-isolation proof and the MW16 genuine-HWC dispatch
test (which already set that variable) keep working, while the default backing is
the `merriam` relay the egress proof (MW18-4) isolates.


## SHIM-D20 — C++ artifact parity is a shared on-disk contract, not a captured binary

The `.pilcfg` sidecar (SHIM-D5) and the `persisted_state` registry snapshot are a
format **shared** with the C++ `mwin32` shim: the two implementations interoperate
by reading and writing the *same* bytes, not by sharing code. Per Design Autonomy,
the parity contract this crate owns is therefore the **on-disk schema**
(`<Platform><Registry>…`, documented at platform-isolation D18/D19, which is the
same schema the C++ shim's `save_xml` emits — mwin32 DESIGN-NOTES D7), not "whatever
the C++ binary happens to output." Two consequences:

- **The C++ shim is registry-only.** Its M1–M4 cover the `.pilcfg` schema and
  persisted *registry* snapshots; it has no filesystem persisted-state. So
  "C++ artifact parity" is scoped to the **registry** snapshot. Filesystem
  persisted-state *through the shim* remains the documented SHIM-D13 gap (there is
  no C++ filesystem artifact to be parity with); the filesystem is isolated via
  `buffer_updates` instead.

- **Parity is proven against a golden artifact in the C++ dialect, loaded through
  the shim.** `testdata/cpp_registry_artifact.xml` is authored to the C++ emission
  dialect the shim never produces itself — abbreviated and long-form hive names, a
  `last_write_time` attribute, every decodable `REG_*` type plus a default value,
  mixed-case hex, value and key tombstones, a mirrored placeholder, and out-of-order
  subkeys. `tests/cpp_parity.rs` drives it through `ShimSession::from_config` and
  asserts the shim's *observable* behavior (decode, hive normalization, tombstone
  fold, mirrored-as-empty, ordinal enumeration, write isolation, source artifact
  read-only). Where `tests/pilcfg.rs` round-trips the shim's *own*
  `save_registry_hive` output, this proves the shim consumes the foreign dialect.
  A literally-C++-binary-captured artifact (driving the CMake/vcpkg C++ build to
  emit a snapshot) is a future swap-in, not a blocker — exactly as the
  platform-isolation golden artifacts already note. The on-disk schema is the
  contract; the producer is interchangeable.


## SHIM-D21 — Deployment model: link inputs + a sidecar, co-located beside the host

Isolating an unmodified application with this shim is a **packaging** act performed
from the outside; the application's source and build are never edited (SHIM-D19).
The deployment unit, proven end to end by `linkproof/` (synthetic C++ client) and
`hwcproof/` (genuine HWC + `wordy`), is four co-located artifacts:

1. **`windows_win32_shim.dll`** — the shim cdylib (the `m`-prefixed exports).
2. **`windows_win32_shim.dll.lib`** — its import library, a *build-time* input.
3. **The alias object** — emitted by `gen-alias-obj` from the checked-in
   `windows_win32_shim_aliases.ndjson` (SHIM-D4); a *build-time* input that rewrites
   the application's `__imp_*` IAT slots to the shim's `m*` exports.
4. **`<host-executable>.pilcfg`** — the runtime sidecar (SHIM-D5/D13), read via
   `current_exe` (so it is keyed to the **host process**, not the module — see
   MW15's "isolate the module, not the host" decision).

The two build-time inputs (2, 3) are injected into the application's link **without
touching its sources** through generic, env-driven `build.rs` glue — `wordy`'s
`WORDY_EXTRA_LINK_{SEARCH,OBJ}` is the reference pattern — scoped to the loadable
**cdylib** so a host binary stays ordinary. At runtime the loader resolves the
shim DLL from the host's directory (where all four artifacts are co-located), and
the shim composes its isolation stack from the sidecar.

**Explicitly out of scope for now** (recorded so the absence is intentional, not an
oversight; none is queued as work):

- **A productized SDK packaging story** — a NuGet/MSI/redist layout, a versioned
  import-lib + headers bundle, and a supported `build.rs`/MSBuild integration story
  for third parties. Today the injection is demonstrated by the in-repo proofs; a
  shippable SDK is a separate effort gated on an actual consumer.
- **A literally-C++-binary-captured `persisted_state` artifact** (SHIM-D20): the
  golden artifact is the shared on-disk contract; capturing one from the C++ build
  is deferred.
- **Filesystem persisted-state through the shim** (SHIM-D13): there is no C++
  filesystem artifact to be parity with, and the shim has no `FilesystemBacking::
  Persisted`; the filesystem is isolated via `buffer_updates`.


## SHIM-D22 — WinHTTP egress seam (outbound relay isolation)

The registry/filesystem/loader/COM seams isolate a process's *local* host calls;
reading a representative relay / host-agent service showed that a relay service's defining behavior is
**egress**, over two client stacks — **WinHTTP** (`winhttp.dll`, the REST
forwarders) and **WWSAPI** (`webservices.dll`, the typed SOAP control-plane). The
first egress seam aliases **WinHTTP** and routes it through the
`windows-platform-isolation` egress surface (D31).

- **Same technique, new imports.** The seam reuses the alias mechanism (SHIM-D4 /
  D24) pointed at `winhttp.dll`: an unmodified relinked client's `WinHttp*` imports
  bind `m`-prefixed exports. No application change.
- **The shim owns lifecycle reassembly.** Unlike the mostly-stateless reg/fs calls,
  egress is a stateful, handle-based, multi-call transaction (`WinHttpOpen →
  Connect → OpenRequest → AddRequestHeaders → SendRequest → ReceiveResponse →
  QueryHeaders → QueryDataAvailable → ReadData → CloseHandle`). The shim keeps an
  `HINTERNET` handle table whose per-handle state accumulates the request, captures
  one `EgressRequest` at the send boundary, and drains the chosen `EgressResponse`
  back across the read calls — the same replay-state-in-a-handle shape as the
  `FindFirstFile`/`FindNext` enumeration (SHIM-D14). The surface (D31) trades only
  in whole request/response values; reassembly never leaks into it.
- **Modes from `.pilcfg`.** An `egress` section selects an `EgressBacking`
  (passthrough / redirect / buffer / replay / block); absent config = transparent
  1:1 passthrough that never builds a surface, so passthrough is a perfect
  link-time identity (D25). `redirect` rewrites scheme/host/port/path then sends for
  real; `buffer` captures mutations and returns a synthetic ack (the network peer of
  `buffer_updates`); `replay` serves preloaded fixtures (the owner's "system state
  pre-loaded").
- **Cross-toolchain proof (`egressproof/`, MW17-5).** The network-seam analogue of
  `linkproof/`: a synthetic C++ client making genuine `WinHttp*` calls, linked
  against the alias COFF object, exercised under each mode against a **closed**
  loopback port (so a genuine send fails fast). `redirect` diverts that dead target
  to a live loopback echo (200 + marker); `buffer` POSTs and gets the synthetic 202
  with no listener anywhere; `replay` serves a 203 fixture with no listener; a
  non-aliased control reaches the real (dead) target and fails. **Verdict travels
  only in the process exit code**, because the alias roster also aliases
  `WriteFile`/`ReadFile`, so the aliased build's CRT `printf` → `WriteFile(stdout)`
  is itself rerouted into the shim's `mWriteFile` and its stdout is **swallowed**
  (the same reason `linkproof` is exit-code-driven). The control build, linking
  genuine `winhttp.lib`, keeps working stdout.
- **WWSAPI deferred (owned scope).** SOAP egress is a later peer seam at the app's
  `Ws*` import boundary — it **cannot** be reached by aliasing `winhttp.dll`, since
  WWSAPI's own WinHTTP calls are internal to `webservices.dll` and outside the
  app's import table. Recorded so the gap is intentional. Realized by **MW17**,
  consuming platform-isolation **M11**; see
  `../windows-platform-isolation/design-sessions/DESIGN-SESSION-2026-06-25-egress-surface-and-validation-tier.md`.

## SHIM-D23 — Validation tier: dictionary-store service + `wordy` split

To exercise the egress seam (SHIM-D22) against a *real* dependent service rather
than a synthetic stub, `wordy`'s on-disk custom dictionary is carved into a
separate web service, turning `wordy`'s calls to it into the egress we isolate.

- **`merriam` (new crate).** A REST dictionary-store service owning the custom
  dictionary on disk — add / update / store / remove / enumerate — with a
  listener-independent dispatch core (testable like `wordy::routes::Service`) and an
  inbound edge over the **HTTP Server API (http.sys)**. http.sys is chosen over a
  third HWC/IIS-native-module to avoid that duplication and to keep `merriam`
  self-hosting and lightly testable; the core is tested off the listener, the
  listener edge is a gated integration test.
- **`windows-file-io` (new crate).** `merriam`'s disk I/O uses **native async
  Win32**: overlapped `CreateFile`/`ReadFile`/`WriteFile` with completion via the
  Windows thread pool (`CreateThreadpoolIo` / `StartThreadpoolIo`, over
  `windows-threadpool`). The API is written **async/completion-shaped even though
  small ops usually complete synchronously** (owner's directive): the synchronous
  fast path is handled, but the code does not assume it. `unsafe` confined to a
  `-sys` leaf (D1/D13 discipline).
- **`wordy` split.** `wordy` drops its local filesystem custom store and **relays**
  the custom-dict ops to `merriam` over WinHTTP, keeping its shared-dictionary
  spell-check / match / anagram / `fst` work. `wordy` stays shim-unaware (SHIM-D19):
  it makes ordinary WinHTTP calls; the egress seam isolates them from the outside.
- **Proof.** End-to-end, the three owner-requested modes are demonstrated against
  the real `merriam`: redirect (URL rewritten to a second instance), buffer
  (mutations captured, `merriam` untouched), replay (reads served from fixtures,
  `merriam` offline). Realized by **MW18**; see the design session above.

**MW18 closure (realized, 2026-06-25).** All four pieces landed:

- `windows-file-io` (+ `-sys` leaf): async overlapped Win32 file I/O over the
  `windows-threadpool` IOCP reactor, async-first even on synchronous completion
  (`D-FIO-1..6`). `merriam`: a content store over `windows-file-io`, a dispatch
  core mirroring `wordy`'s custom routes 1:1, and an http.sys listener
  (`MER-D1..5`). `wordy` relays to `merriam` over ordinary WinHTTP through a
  `CustomStore` trait (`WD-D13`); the FS store was **re-homed, not deleted**, so
  the MW15/MW16 proofs (which set `WORDY_CUSTOM_ROOT`) keep working while the
  default backing is the relay the egress seam isolates.
- The capstone proof (`egressrelayproof/run-egressrelayproof.ps1`) links the
  `wordy-relay-probe` bin — driving `wordy`'s **real** `MerriamClient` relay —
  against the alias object, runs a genuine `merriam`, and verifies all four
  scenarios with the probe **exit-code-driven** (its stdout is swallowed by the
  aliased `mWriteFile`, like `linkproof`/`egressproof`) and `merriam`'s state
  asserted by a **non-aliased** direct query: redirect → a dead port diverted
  into `merriam` (the word lands there); buffer → the POST captured, `merriam`
  untouched; replay → a fixture served with no network; control (non-aliased) →
  the real dead target, transport failure. Gated `tests/egress_relay.rs`
  (`#[ignore]`, SKIP on an unbindable URL). A manual companion
  (`launch-tandem.cmd` / `.ps1`) brings the two services up under any egress mode.
  **Verified 4/4 on this host.** This is the egress analogue of MW15's filesystem
  isolation proof, against a *real* dependent service rather than a synthetic stub.

## SHIM-D24 — API-interaction journaling seam (AJ-B)

When the `.pilcfg` `api_journal` block is enabled, the shim journals observed
HTTP interactions as NDJSON for off-machine post-processing by the `cartographer`
OpenAPI tool. The on-disk record schema and the shapes-only body model are owned
by the shared `api-journal` crate (its `D-AJ-1..3`), so the shim (writer) and the
tool (reader) cannot drift.

- **Config (`pilcfg.rs`).** A new `api_journal` block parses to `ApiJournalConfig`
  (`enabled`, `%VAR%`-expanded `path`, `bodies` = `shapes`|`full-with-pii`|`none`,
  `seams.{inbound,egress}`, `max_body_bytes`). Disabled by default; the strict
  parse / tolerant load posture of the rest of the sidecar is preserved. Body
  default is **shapes-only** — a JSON schema skeleton with no literal scalar
  values — so journals describe an API's structure without exporting user data.
  `full-with-pii` additionally captures a literal example body via
  `JournalSink::body_example` (AJ-DEF-1 / `D-AJ-3`); examples are literal user
  data, captured only under the opt-in `full-with-pii` mode.
- **Sink (`journal.rs`).** A process-wide `JournalSink` opens the file lazily,
  serializes record writes behind a `Mutex`, and stamps each record's
  `session_id` / `seq` / `timestamp_ms`. It writes through ordinary `std::fs`:
  link-time aliasing redirects only the *client's* `WriteFile`/`CreateFileW`, so
  the shim's own I/O binds the real kernel32 and journaling never recurses through
  `mWriteFile`. Fail-soft — an unopenable path or a write error drops the record,
  never the host.
- **Two decorators, one sink.** `JournalingEgress` wraps the session's
  `EgressBacking` (so the outbound WinHTTP seam journals each successful
  exchange), and `JournalingHandler` wraps the per-request inbound
  `RequestHandler` stack built by `WebState::build_handler` (so the IIS seam
  journals each request/response). The session builds **one** sink and shares its
  `Arc` with both seams, filtered by `seams.{egress,inbound}`. Inbound journaling
  is independent of the legacy `WebMode` observation channel: it fires whenever
  the inbound seam is enabled, because the host rebuilds the handler per request.
- **Metadata policy.** Paths are literal (the tool infers `{templates}` from
  concrete paths); query parameters keep names + value *shapes*; headers keep
  names, with literal values retained only for the content-negotiation safelist
  (`Content-Type`, `Accept`). Egress records carry the destination
  scheme/host/port; inbound records do not (the service is the host).
- **Proof.** Hermetic `tests/journal_capture.rs` builds a real `ShimSession` from
  an `api_journal`-enabled `.pilcfg` (egress `buffer` mode, so a POST is acked
  synthetically with no network), drives both seams, and asserts the shared file
  holds one `Seam::Egress` + one `Seam::Inbound` record with one `session_id` and
  monotonic `seq`. The aliased/live end-to-end path stays available as
  `egressrelayproof`.

## SHIM-D25 — Off-thread (toward out-of-process) interception handling via marshaled work items

The work the shim does in response to intercepting a web API call — today journaling,
later isolation decisions — is being moved off the calling thread and, eventually, out
of process. Staged in [`CHECKLIST-offproc.md`](CHECKLIST-offproc.md).

- **Marshal a position-independent context.** At each seam the intercepted interaction
  is marshaled to a **JSON** request (seam, method, scheme/host/port, path+query,
  headers, bodies as base64, status) — a form that could cross a process boundary
  unchanged. The worker returns a JSON reply (an outcome/ack today; a possibly-modified
  response later).
- **Enqueue to the Windows thread pool; this step waits synchronously.** The marshaled
  request is handed to a `windows-threadpool` work item. In this first step, *regardless
  of the caller's contract*, the calling thread blocks until the item finishes, via a
  `WaitOnAddress` / `WakeByAddressSingle` `WaitGate` latch (a primitive that generalizes
  to the eventual cross-boundary completion handoff). Honoring the real contract (async
  for fire-and-forget) is a later stage.
- **Move all the work, but keep the persisted artifact reduced.** The worker performs the
  full reduction (shapes-only bodies, safelisted headers) and writes the record, so the
  *on-disk* journal is byte-identical to the inline path — no persisted-PII regression.
  The marshaled request is *raw* and transient; when the worker moves out of process the
  raw context crosses the channel, so PII tokenization (D-AJ-4 / PII-A) becomes the
  worker's job before persisting.
- **The latch lives in `windows-threadpool`, not the shim.** The shim keeps its `unsafe`
  quarantined at the ABI/alias boundary (SHIM-D2); the `WaitOnAddress` FFI is quarantined
  in the thread-pool crate's existing `ffi` module instead.
- **Bounded, compact marshaled bodies (milestone BC).** The seam carries only the leading
  `max_body_bytes` of each body (`JournalSink::capped_body`) — the worker never inspects
  past that cap, so truncating at the seam is behavior-preserving yet bounds the payload
  instead of cloning and encoding whole multi-MB bodies. Bodies are encoded as **base64
  strings** in the marshaled JSON (not number arrays), ~3× smaller and the standard
  binary-in-JSON form for the eventual cross-process payload (`base64_body` serde adapter
  in `marshal`).

## SHIM-D26 — Untranscoded, encoding-tagged text end-to-end (milestone UT, planned)

**Governing principle (directive):** *never impose an encoding or transcoding burden on data
captured from the platform (http.sys / WinHTTP / file / registry / etc.). Carry it verbatim in
its native encoding, tagged with what that encoding is, and let only the final consumer decode.*

Today the egress seam violates this: it eagerly converts WinHTTP wide strings (`Utf16`) to UTF-8
(`to_utf8()` in `egress_interaction` / `raw_egress_headers`) — burning host CPU, silently dropping
ill-formed UTF-16 (`unwrap_or_default()`), and converting header values the worker then discards.
Planned in [`CHECKLIST-offproc.md`](CHECKLIST-offproc.md) (Milestone UT). Spans `api-journal`
(schema), this shim (producer), and `cartographer` (consumer).

- **The platform is not uniformly UTF-16; it splits by API family.** Win32 *client* wide APIs
  (WinHTTP egress, file, registry) are UTF-16. The HTTP *server* stack (http.sys / IIS) is
  **narrow raw octets** because HTTP is a byte protocol — `wordy` reads `pRawUrl` (`*const u8`)
  and `IHttpRequest::GetHeader` (`PCSTR`); `merriam` reads `HTTP_KNOWN_HEADER` (`PCSTR`). So
  the inbound `String` fields in `windows-platform-isolation::web` are narrow-native, not a
  hidden wide source. The encoding tag records this faithfully instead of forcing one encoding.
- **Carry raw bytes + a 1-byte encoding tag, never transcode in the service.** A shared
  `RawStr { enc, bytes }` (in `api-journal`, the schema owner) with `enc ∈ { Utf16Le, Bytes }`
  represents every captured text field. Egress wraps `Utf16::as_units()` as `Utf16Le` (no
  `to_utf8`); inbound wraps its narrow bytes as `Bytes`. Ill-formed UTF-16 is preserved
  losslessly; dropped header values are never transcoded.
- **Persist the tagged bytes; decode only in the reader (Option C).** The NDJSON journal stores
  `RawStr`, so the *producer* (and the eventual out-of-process collector) transcodes nothing;
  `cartographer` decodes per tag (`Utf16Le`→UTF-8 lossy, `Bytes`→UTF-8 lossy) at read time.
  This pushes the one unavoidable UTF-8 conversion to off-machine post-processing.
- **Always raw+tagged; no *gratuitous* re-encoding.** `RawStr` serializes *uniformly* as a tagged
  object `{ "enc": "u16"|"raw", "b64": "…" }`. We do **not** sniff the bytes for UTF-8 validity to
  opportunistically emit a plain string: that inspection buys only journal *readability*, which has
  no value to the producer (readability is the reader's job). This is a value judgment about a
  pointless transcode — **not** a blanket rule against ever looking at the data (see PII below).
  Consequence (accepted): the raw NDJSON is opaque for these fields until `cartographer` decodes.
- **Inspection *is* allowed when it has value — notably PII redaction, which must stay in-process.**
  The principle bans a transcoding/encoding burden imposed *for storage or transport*; it does not
  ban examining the data. We anticipate needing to inspect captured data to **redact PII before it
  exits the process** (a real privacy guarantee, unlike readability sniffing). The hard constraint:
  redaction must happen **before** the marshaled payload crosses the process boundary — so it cannot
  be deferred to the out-of-process collector (the raw data would already have left). This *refines*
  the SHIM-D25 sketch of "carry raw across the channel, tokenize in the collector": if redaction must
  precede process exit, that step remains in-process even after the rest of the worker moves out.
  Exactly where redaction sits (seam vs. in-process pre-send worker) and how it interacts with the
  encoding tag is an **open design point** (D-AJ-4 / PII-A) to settle before the out-of-process stage.
- **base64 is transport packaging, not transcoding.** Putting raw bytes into JSON requires
  base64; this is reversible and byte-preserving (the data's encoding is untouched), unlike the
  forbidden `Utf16→Utf8` transcode which reinterprets and can fail. base64 of captured bytes
  still costs an O(n) pass, so it must run on the worker, not the calling thread (see the
  marshal-without-serializing note below).
- **The worker reduces on a transient decoded view.** Reductions that need UTF-8 string ops
  (split path/query on `?`/`&`/`=`, header safelist `ascii_lowercase`, content-type match,
  `infer_scalar`) decode transiently inside the worker; the *stored* field stays raw tagged
  bytes. Decoding for a parsing decision is not the same as persisting a transcoded string.
- **Don't serialize captured data on the calling thread (UT-B0).** `dispatch_off_thread` today
  calls `interaction.to_json()` (which base64-encodes bodies) on the *host* thread before
  submitting. To honor the principle, the host hands the worker the raw in-memory `Interaction`
  (a move, zero encoding) and the worker serializes off-thread; serialization re-enters only at
  the real IPC boundary when the worker moves out of process. (The in-process *reply*
  serialization stays as-is per the prior directive — that is our control data, not captured
  platform data.)
- **SOAP reinforces these decisions (future).** Extending to a SOAP-speaking Azure agent will make
  the body load-bearing for *operation identity* (the `<soap:Body>` child / `SOAPAction`), parsed
  in-process. XML self-describes its encoding (prolog), so raw-bytes+tag is exactly right — the
  parser honors the declared encoding, we never pre-transcode. Two forward constraints (reserved,
  see the UT "Open design points"): `cartographer` needs an XML shape model + body-based operation
  identity, and the BC body cap must not truncate before the operation element.

## SHIM-D27 — Failure policy is mode-dependent, not blanket fail-soft (resolves RS-4)

How the observed service reacts to an **observability-pipeline** failure (the journaling I/O today;
the IPC to the collector when out-of-process) depends on the *mode*, not a single fail-soft rule.
This corrects the prior assumption that journaling must always be best-effort.

- **Journaling (capture to derive an API shape): faithful-or-fatal.** A dropped message means the
  log is not faithful to the interactions, which silently corrupts the derived shape. If the user
  asked us to journal and we cannot (disk full, unwritable path, write error; later: collector
  unreachable), that is a **clear, diagnosable failure that aborts the process** — not a swallow.
  This means the current `sink.record` fail-soft swallow is *wrong* for journaling and must become
  fail-loud.
- **Out-of-line primary tasks (redirect / replay / fault — moving the message IS the function):**
  failure likewise raises a **diagnosable fault** (panic with a message, an event-log entry, or a
  log line — severity per how desperate we are when it's detected). The reply is load-bearing here,
  so `Outcome` carries persisted-vs-dropped only in these modes.
- **Tracing (implied best-effort): loss does not fail processing; proceed.** Tracing is queued
  **asynchronously and continues in-process without the OOP round-trip** — i.e. it skips the
  synchronous `WaitGate` hop entirely (this is the deferred "honor the caller's contract / async"
  path).
- **Interaction with RS-1/RS-2:** those contain *unexpected* panics (bugs) so a worker panic can't
  abort the host. A journaling/primary I/O failure is a *deliberate, diagnosed* abort — a different
  thing — so panic containment stays; this adds explicit fatal handling for capture failure.
- **`mode` becomes first-class `.pilcfg` state.** Implementation (mode-aware failure policy) is
  future work tied to the out-of-process milestones; the decision is recorded now.

## SHIM-D28 — Per-seam async capability classification (milestone AC)

UT-B0 took transcoding and JSON off the calling thread, but a bounded snapshot remains: each seam
memcpy's the capped bodies + headers into an owned `Interaction` before hand-off, because we do not
block and the caller's buffers can be freed/reused the instant we return. That copy can only be
removed where the seam's contract permits an **async reply** — retain the platform buffers, let the
worker read them in place, and complete the request after the worker consumes them. Milestone AC.

- **Capability is a property of the seam, not the journal.** Whether a seam can complete
  asynchronously is fixed by the platform contract behind it, so the classifier keys off the
  marshaled `Seam`, the position-independent identity carried across the boundary. Today:
  `Seam::Egress` (WinHTTP `WinHttpSendRequest`/`Receive` driven synchronously by the relay) is
  **sync-only**; `Seam::Inbound` (IIS, which supports `RQ_NOTIFICATION_PENDING` + `PostCompletion`,
  per wordy MW13) is **async-capable**.
- **Gate now, diverge later.** AC-1 only introduces the flag and branches the capture path on it.
  Sync-only seams keep the exact snapshot-then-block path unchanged; the async branch currently
  falls through to the same blocking dispatch, so behavior is identical until AC-2 supplies the
  buffer-retention + non-blocking-completion primitives. This keeps the classification testable and
  the seam routing in place without changing observable behavior.
- **Why gate before building.** The zero-copy path needs buffer-lifetime ownership and a completion
  registration that differ per seam; isolating *which* seam takes which path first keeps AC-2..AC-4
  small and lets sync seams stay provably untouched. Backpressure on the async path follows the
  SHIM-D27 mode policy (journaling fail-loud, primary fault-diagnosable, tracing best-effort drop).
- **Ingress is wired non-blocking (AC-4).** The inbound IIS seam dispatches via `dispatch_retained`
  and parks the `RetainedCapture` in the per-request handler, so the request thread returns without
  blocking; the worker journals and the handler's drop (PostCompletion analog) joins. Egress stays
  sync (AC-3). The bounded body copy remains until the OOP stage retains true platform buffers.

