# Design session — 2026-06-22 — `FilePath` surface for M6

**Resulted in:** D22 (filesystem path model mirrors the C++ `m::pil::file_path`;
the C++ unit tests are the conformance spec). Reshaped CHECKLIST M6-1 into
M6-1 (root parsing) + M6-2 (path algebra), mirroring the C++ `M-FS-PATH-1` /
`M-FS-PATH-2` split.

## Question

What must the Rust `FilePath` expose? Specifically: how do we deal with path
separators, and do we need root finding? Speculation going in was that we might
need experimental C++ against the Microsoft STL to see what `std::filesystem::path`
does.

## Finding 1 — no STL spelunking needed

The C++ `m::pil::file_path`
(`src/libraries/pil/include/m/pil/file_path.h`, `.../src/file_path.cpp`) is a
**self-contained model**. It reimplements `std::filesystem::path` semantics for
two explicit surfaces (`path_surface::{windows, posix}`) rather than delegating
to the host STL at runtime — the PIL models a *chosen* platform that need not be
the host. So `test_file_path.cpp` is the executable spec; we mirror its cases
and never need to probe the real STL.

## Finding 2 — separators are surface-parameterized

Constants `file_preferred_separator = u'\\'`, `file_posix_separator = u'/'`.

- Windows surface: both `\` and `/` separate; `lexically_normal` rewrites `/`→`\`.
  `\\?\` (extended) and `\\.\` (device) prefixes require **literal backslashes**
  (`is_windows_separator`); everything past an extended-length prefix is verbatim.
- POSIX surface: only `/` separates; `\` is an ordinary filename char; single `/`
  root, leading-slash runs collapse to one.
- Storage is raw/lossless and round-trips through `native()`; normalization only
  happens in `lexically_normal`, never on construction. (Same exact-store /
  fold-on-compare split as `key_path`.)

## Finding 3 — the API surface the PIL filesystem actually depends on

From the *production* consumers (not the type's own tests):

- Root decomposition (universal `open_root(fp.root())` + `open_directory(fp.relative_path())`):
  `root()`, `relative_path()`, `root_kind()`, `is_absolute()` —
  `materializing/materializing_webcore.cpp`,
  `direct/Platforms/windows/win32_filesystem.cpp`.
- Path breaking (buffered overlay tree walk): `has_parent_path()`,
  `split_parent_path_and_leaf_name() -> (Option<parent>, leaf)`, `parent_path()`,
  and `native()` as the overlay map key —
  `buffered/directory_mutation_operations.cpp`,
  `buffered/directory_read_operations.cpp`.
- `file_root` companion: `kind()`, `text()`, `is_none()`,
  `suppresses_normalization()`, `is_fully_qualified()`.
- Provider-facing algebra (locked down by tests, needed by providers):
  `lexically_normal`, `equivalent`, `precedes`, join `operator/`.

## Finding 4 — root parsing is the heart (`parse_root`)

Seven-way classification, no normalization (classify as-is so input round-trips):
drive (`C:` rel / `C:\` abs), `\\server\share` UNC (incomplete `\\server`
tolerated), `\\.\` device, `\\?\` extended, `\\?\UNC\` extended-UNC (UNC token
matched ASCII-case-insensitively), single-slash POSIX, else relative. Stored as
`(m_value, m_root_kind, m_root_length)`: `root = m_value[0..root_len]`,
`relative = m_value[root_len..]`.

`..` resolution (`resolve_dot_segments`): pops a prior segment; underflow past a
fully qualified root is **rejected** (`invalid_parameter`), never clamped;
leading `..` in a relative path is preserved.

## Decision

Adopt the C++ model wholesale for **alignment now** with the shared providers /
artifact; defer finer divergences (e.g. whether the Rust crate ever needs the
POSIX surface, or only Windows). Windows-surface comparison routes through the
existing M2 `OrdinalCasing` seam (`CompareStringOrdinal`, D6), so NTFS
case-insensitivity needs no second casing mechanism. Filesystem errors stay a
separate hand-rolled type (D14).
