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
