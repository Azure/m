# mwin32 design notes

## D1 — Session bootstrap and predefined-key resolution

The shim must turn a Win32 predefined `HKEY` (e.g. `HKEY_CURRENT_USER`) into a
live PIL `ikey` before any `mReg*` call can do useful work. There is no Win32
"open the root" call; the predefined keys are always-open pseudo-handles.

Decisions:

- A single process-wide `m::mwin32_impl::session` (function-local `static`, so it
  is created lazily and thread-safely on first use) owns the PIL stack:
  `iplatform` + `iregistry`. The default configuration is **passthrough** to the
  live Win32 registry. (`.pilcfg`-driven configuration is a later milestone.)
- The session opens predefined keys via `iregistry::open_predefined_key` and
  caches the resulting `ikey` per `predefined_key` for the process lifetime.
- Resolution is done at a **chokepoint**, not at the ~15 `mReg*` call sites:
  `handle_table::deref_handle<shared_ptr<ikey>>` first asks the session whether
  the raw handle value names a predefined key, and only falls back to the
  interned-handle table otherwise. This keeps the call sites untouched and means
  every present and future `mReg*` that dereferences an `HKEY` gets predefined
  support for free.
- `handle_table::close` (and therefore `RegCloseKey`) is a **success no-op** on
  predefined pseudo-handles — they are never interned and must stay open.

## D2 — `make_platform_interface` on the PIL public API

The friendly value-wrapper facade (`m::pil::platform` / `registry_class` /
`key`) deliberately does not expose the raw `ikey`. A Win32 shim operating at
the interface layer needs the raw interfaces, so PIL now offers
`m::pil::make_platform_interface(flags, redirections)` returning
`std::shared_ptr<iplatform>` directly. `make_platform` is implemented in terms of
it, so the flag-mapping lives in one place.

## D3 — Predefined HKEY values are sign-extended pointers

The Win32 predefined `HKEY` constants are defined as
`(HKEY)(ULONG_PTR)(LONG)0x8000'000N`. Because the 32-bit literal has bit 31 set,
the cast through `LONG` sign-extends it: on 64-bit the actual pointer value is
`0xFFFF'FFFF'8000'000N`, **not** `0x0000'0000'8000'000N`. The value→`predefined_key`
mapping therefore recovers the low 32 bits (and verifies the upper bits are the
sign-extension of bit 31) before comparing against the enum. Interned table
handles are minted as small positive values and never collide.

## D4 — Test executables need the shared mwin32 DLL copied alongside

`m_mwin32` is a `SHARED` library built in a sibling directory from the test
executable, so the test cannot locate it (or its transitive runtime DLLs) at
launch/discovery time (`gtest_discover_tests` runs the exe). The test
`CMakeLists.txt` copies `$<TARGET_RUNTIME_DLLS:test_mwin32>` next to the test
binary as a `POST_BUILD` step. Note: this copy only refreshes when the test exe
relinks, so after an incremental change that rebuilds only the DLL, a clean build
(or relink) is needed for the copy to update.

## D5 — `.pilcfg` sidecar configuration

The session's PIL stack is selected at startup from an optional
`<host-executable>.pilcfg` JSON file located next to the process executable
(`GetModuleFileNameW(nullptr, ...)`, so it is keyed to the .exe, not this DLL).

Schema (all members optional, default `false`):

```json
{ "buffer_updates": false, "record_modifications": false }
```

These map 1:1 onto `m::pil::make_platform_flags` — `buffer_updates` → buffered
layer (mode "buffered"), `record_modifications` → logging layer (mode
"logging"), neither → passthrough (mode "passthrough"). The schema intentionally
mirrors the factory exactly rather than inventing higher-level mode names, so the
config cannot express something the stack cannot do.

Decisions:

- The parser (`parse_pilcfg`, `pilcfg.cpp`) is **strict**: invalid JSON, a
  non-object root, or a recognized member with a non-boolean value all throw.
  Unknown members are ignored (forward compatibility).
- The loader (`load_pilcfg`) is **tolerant**: any failure — file absent,
  unreadable, or malformed — falls back to the passthrough default rather than
  throwing. A broken or missing sidecar must never break the host process.
- `parse_pilcfg` takes `std::string_view` and returns a `pilcfg` struct with no
  PIL dependency, so it is unit-testable in isolation. It is not exported from
  the DLL (the `.def` exports only `mReg*`), so the test compiles `pilcfg.cpp`
  directly and adds `../src` to its include path.
- **Out of scope for now** (the factory cannot support them as runtime config):
  redirections (the PIL redirections parameter is an `std::initializer_list*`,
  which cannot be built from variable runtime data — needs a `std::span`
  refactor) and persisted-state load (no such capability exists in the buffered
  layer yet). Both are queued as milestone M4 in CHECKLIST.md.

## D6 — Registry value operations and ANSI/UTF-16 data conversion

`mRegSetValueEx*` / `mRegQueryValueEx*` map onto `ikey::set_value` /
`ikey::get_value`. The value bytes stored in the registry are exactly the bytes
handed to `set_value`; the `*W` entry points therefore pass the caller's buffer
straight through, and `reg_value_type` is `static_cast` to/from the Win32
`REG_*` `DWORD` (the enum's values are defined to equal the `REG_*` constants).

Decisions:

- **Owned query contract.** The shim reproduces the Win32 `RegQueryValueEx`
  contract on top of `get_value` (which reports a too-small buffer via
  `new_bytes_required` and trims its out-span to the actual size on success):
  - `lpData == NULL` → size/type query: report the required size in `*lpcbData`
    and the type in `*lpType`, return `ERROR_SUCCESS`.
  - buffer too small → `*lpcbData` = required size, return `ERROR_MORE_DATA`.
  - success → copy data, `*lpcbData` = actual size, return `ERROR_SUCCESS`.
  Because `get_value`'s more-data path does not guarantee the type is populated,
  the type is fetched separately via `get_value_type` on that path.
- **Reserved validation is non-throwing.** A non-zero `Reserved` (or non-NULL
  `lpReserved`) returns `ERROR_INVALID_PARAMETER` directly rather than going
  through `M_VALIDATE_PARAMETER`, whose `m::invalid_parameter` is **not** a
  `std::system_error` and would escape the C ABI uncaught.
- **`m::not_found` → `ERROR_FILE_NOT_FOUND`.** The buffered provider throws
  `m::not_found` (an `m::runtime_error`, not a `system_error`) for a missing
  value, so the query functions catch it explicitly. The direct provider's
  `ERROR_FILE_NOT_FOUND` already arrives as a `system_error` and is unmapped by
  `decode_win32_error`.
- **ANSI string DATA conversion (`*A`).** For the textual value types
  (`REG_SZ`, `REG_EXPAND_SZ`, `REG_LINK`, `REG_MULTI_SZ`) the `*A` entry points
  convert the value *data* between CP_ACP and the UTF-16 stored form, so an
  `*A` writer and a `*W` reader (or vice-versa) agree. The whole buffer is
  converted in one call (`MultiByteToWideChar` / `WideCharToMultiByte` with an
  explicit length), which preserves embedded and trailing NULs and so handles
  `REG_MULTI_SZ` uniformly with the single-string types. Non-string types carry
  their bytes through unchanged. Query buffer sizes for `*A` string values are
  expressed in ANSI bytes, matching the ANSI caller's expectation.
- **Test isolation via buffered mode.** The value-op integration tests must not
  touch the live registry, so the test executable ships a generated
  `test_mwin32.exe.pilcfg` enabling `buffer_updates`. Writes target the
  predefined `HKEY_CURRENT_USER` handle directly; the buffered overlay records
  them in memory and reads them back without ever persisting. Each test uses a
  distinct value name because the buffered session is process-wide and persists
  across tests. The buffered layer delegates `open_key` and unmodified value
  reads to the underlying live key, so the read-only predefined-key tests still
  pass under `buffer_updates`.

## D7 — `.pilcfg` redirections and persisted-state snapshots (mode (c))

Milestone M4 completed the two capabilities deferred from D5: runtime-built
registry redirections and running against a persisted registry snapshot without
touching the live system.

Decisions:

- **Redirections are runtime config.** The PIL redirections parameter was
  changed from `std::initializer_list<...>*` to
  `std::span<std::pair<std::u16string_view, std::u16string_view> const>` across
  `make_platform` / `make_platform_interface` / `create_platform_interface` /
  `redirecting::platform` / `redirector` so it can be driven by variable data.
  The `.pilcfg` schema gains an optional `redirections` array of
  `{ "from": "...", "to": "..." }` objects; the session owns the parsed strings
  (in `pilcfg`) and hands PIL `string_view`s into them (PIL copies them into the
  redirector at construction).

- **Persisted-state load (mode (c)).** The `.pilcfg` schema gains an optional
  `persisted_state` string naming an XML snapshot file. When set, the session
  builds its platform from that file via `m::pil::load_platform_interface` and
  **ignores** the layer flags and redirections: reads and writes run entirely
  against the loaded snapshot over a *null* underlying registry, so the live
  system is never touched. The selection logic lives in
  `m::mwin32_impl::build_platform_from_config(pilcfg const&)`, factored out of
  the process-wide session so it can be unit-tested without a real sidecar.

- **Snapshot persistence format.** The buffered layer owns the change-log
  overlay and is the only layer that persists state. `buffered::platform::save`
  serializes the overlay into the supplied `<Platform>` element, then lower
  (change-log-free) layers pass through. The schema is:

  ```xml
  <Platform>
    <Registry>
      <Key name="HKCU">               <!-- predefined key, mapped name -->
        <Key name="Software">         <!-- materialized subkey -->
          <Value name="age" type="4" data="18000000"/>
          <Value name="name" type="1" data="4a006f0065000000"/>
          <Key name="Gone" deleted="true"/>     <!-- key tombstone -->
          <Value name="Old" deleted="true"/>    <!-- value tombstone -->
        </Key>
      </Key>
    </Registry>
  </Platform>
  ```

  `type` is the `reg_value_type` integer (= the `REG_*` constant); `data` is the
  value's raw bytes as lowercase hex. Mirrored placeholder nodes (subkeys/values
  the overlay has merely observed from the underlying registry but not modified)
  are **not** serialized — only materialized writes and tombstones are. On load,
  every persisted key/value is reconstructed as a fully-materialized
  (non-mirrored) node so the snapshot is self-contained.

- **pugixml is in `PUGIXML_WCHAR_MODE`.** `char_t` is `wchar_t`, so every
  attribute value written must be a wide string. Passing a narrow `const char*`
  to `xml_attribute::set_value` silently selects the `bool` overload and emits
  `="true"`. All names/hex are produced via `m::to_wstring(...)`; this is a
  recurring trap worth remembering when touching the serializer.

## D8 — Link-time Win32→mwin32 redirection ("alias object")

Status: **confirmed, built, and link-proven** (milestone M-ALIAS in CHECKLIST.md).
The M-ALIAS-1 spike confirmed the mechanism and symbol spelling; M-ALIAS-2/3/4
built the generator, the `mwin32_alias` OBJECT library, and a link-proof
integration test in which genuine `<windows.h>` registry calls (no mwin32 headers)
redirect into the shim's buffered overlay while the live registry stays untouched.

**Problem.** The shim's value to a client today requires the client to call the
`mReg*` names (or include `<m/mwin32/mWindows.h>`). Unmodified code that calls the
genuine `RegCreateKeyExW` reaches advapi32, not the shim. We want a **supported,
user-mode, link-time** way to redirect those calls with no source edits — and
explicitly **not** the runtime-patching route (Detours) or the kernel route. The
redirection problem is arbitrarily hard if approached as "intercept everything";
we deliberately take the shallow, supported slice that covers what the client
*links*, and stop there.

**Mechanism (confirmed by the M-ALIAS-1 spike, MSVC x64).** Ship clients an
*alias object* (`mwin32_alias`) they add to their link line alongside the shim's
import library `m_mwin32.lib`. It contains no logic — only the symbols/directives
that retarget each Win32 reg name onto its `mReg*` counterpart. There are two
client reference forms, and the spike established what each requires:

1. **`dllimport` reference — the decisive one.** Once `<windows.h>` is included
   (the universal real case) `RegCloseKey` is declared `__declspec(dllimport)`, so
   a call compiles to `call qword ptr [__imp_RegCloseKey]`. We **define that data
   slot ourselves**, aimed at the shim. The slot is emitted **signature-free** as a
   pointer-sized `void(*)()`:
   ```cpp
   extern "C" void mRegCloseKey();
   extern "C" void (*__imp_RegCloseKey)() = &mRegCloseKey;
   ```
   The IAT slot is just a pointer; the client's own `<windows.h>` declaration casts
   through its typed call site, so the alias never needs the real signatures (which
   is what lets one generator emit all 82 slots uniformly from the `.def`). The
   spike confirmed this redirects the call to our function **and that advapi32's
   `RegCloseKey` member is never pulled** — defining `__imp_RegCloseKey` ourselves
   satisfies the symbol, so there is no duplicate-symbol conflict even with
   `advapi32.lib` on the default link line. (`__imp_<name>` is the x64 spelling — C
   linkage, no stdcall decoration.)

2. **Plain (non-`dllimport`) reference — best-effort fallback only.** If a client
   declared the function itself without `dllimport`, the reference is to the
   undecorated thunk `RegCloseKey`. We emit
   `#pragma comment(linker, "/alternatename:RegCloseKey=mRegCloseKey")`, but the
   spike proved this is a **weak fallback that loses to advapi32**: when
   `advapi32.lib` is on the link line its strong `RegCloseKey` thunk satisfies the
   reference and `/alternatename` is ignored (the spike's plain call returned
   advapi32's `ERROR_INVALID_HANDLE`, bypassing the shim). `/alternatename` only
   takes effect when the name is otherwise undefined (advapi32 absent). We emit it
   anyway because it is harmless and helps that case, but we do **not** promise
   plain-form redirection over advapi32. In practice this is a non-issue: real
   Win32 clients include `<windows.h>` and therefore hit form (1).

**Alias target = the shim's undecorated import name, via a dedicated undecorated
import library.** The alias references undecorated `mReg*` symbols, but the
**auto-generated** `m_mwin32` import library does *not* expose them: the `mReg*`
functions have C++ linkage, so that import library carries only their **decorated**
names (e.g. `?mRegCloseKey@@YAJPEAUHKEY__@@@Z`). Discovered at M-ALIAS-4 when 82
undecorated references went unresolved.

The first fix attempted — giving the shim `extern "C"` linkage to emit clean
undecorated exports — was **rejected**: several `mReg*` functions intentionally
re-throw `std::system_error`, and under `/EHsc` an `extern "C"` function is assumed
not to throw (C4297). Forcing `extern "C"` would silently change the shim's
exception contract, which is out of scope for a redirection feature.

Instead we keep the shim's C++ linkage unchanged and build a **second, undecorated
import library** from the same `mwin32.def`:
```
link /lib /def:mwin32.def /name:m_mwin32.dll /out:m_mwin32_alias_import.lib /machine:x64
```
Because `mwin32.def` exports by **undecorated** name, this import library exposes
undecorated `mRegCloseKey` thunks and `__imp_mRegCloseKey` slots that bind to the
DLL's undecorated export-table entries at load time. The `mwin32_alias` OBJECT
library links **both** this undecorated import library (to resolve the alias's
undecorated references) **and** the `m_mwin32` target (so consumers transitively
track and copy `m_mwin32.dll` at runtime — CMake does not infer the DLL dependency
from a bare import-library path).

**advapi32 contract.** Defining `__imp_<name>` for every redirected function means
advapi32.lib is never pulled for those names, so link order versus advapi32 does
not fight us and there is no conflict. Functions *not* in the alias set still
resolve from advapi32 normally — no conflict, because we define only the slots we
redirect.

**Single source of truth.** The alias translation unit is **generated from
`mwin32.def`** so the alias set and the export set cannot drift. The `mReg<Name>`
→ `Reg<Name>` map is mechanical (strip the leading `m`).

**Deliberate scope / limitations.** This is opt-in, link-time, user-mode only.

- It redirects what the client *links* through the `__imp_` slots it defines. It
  cannot redirect a reference already compiled into a third-party static library
  that hard-references advapi32's `__imp_` slots, nor calls made via
  `GetProcAddress`/`LoadLibrary`, nor the advapi32→kernelbase API-set layering.
  Reaching those is the "do more" envelope — a runtime interceptor (Detours)
  delegated the messy coverage chase, evaluated separately and only if the
  link-time slice proves insufficient.
- The `mReg*` bodies, the `.def`-as-source-of-truth, and the passthrough
  (mode (a)) trampoline that this milestone hardens are exactly what a future
  Detours delivery vehicle would reuse, so the link-time work de-risks that path
  rather than competing with it.

Primary motivating scenarios: deterministic test isolation first, then
combined filesystem+registry mocking for file-heavy work, where redirecting a
process's own registry calls to a buffered/persisted overlay is the whole point.

## D9 — `.pilcfg` fault-script selection (D8 fault layer from config)

Milestone M-FAULTCFG makes the PIL fault layer (the fault-injecting decorator,
PIL D8) selectable from a `.pilcfg` sidecar, so the shim — and any client whose
registry calls are redirected through it — can be driven through scripted failure
paths without a code change.

Decisions:

- **Schema member.** The `.pilcfg` schema gains an optional `fault_script`
  string naming a `<FaultScript>` XML file (the PIL fault artifact, PIL D8 /
  M-FAULT-1). Absent → no fault injection. Parsing is **strict** like every other
  member (`parse_pilcfg`: present-but-non-string throws), consistent with D5.

- **Layering.** `build_platform_from_config` wraps the fault layer **around the
  base stack the other settings already selected** — live, buffered, redirected,
  or a persisted snapshot (mode (c)). The fault decorator composes over any of
  them via the public `m::pil::apply_fault_layer`, so fault injection is
  orthogonal to which base mode is active (e.g. a replay run against a snapshot
  can still be made to fail the Nth `RegCreateKeyExW`).

- **Tolerant load (consistent with D5/D7).** Loading the referenced fault file is
  best-effort: a missing or malformed `fault_script` leaves the base stack
  **unwrapped** rather than throwing. The session must not be broken by a bad
  fault config any more than by a bad sidecar. The tolerance lives in
  `build_platform_from_config` (a `try/catch` around `load_fault_script` /
  `apply_fault_layer`), distinct from the strict `parse_pilcfg`: the *reference*
  is parsed strictly, the *referenced file* is loaded tolerantly — the same
  split D5/D7 draw between `parse_pilcfg` and `load_pilcfg`.

- **Public surface used.** The wiring depends only on the PIL public fault façade
  (`m/pil/fault.h`: `load_fault_script`, `apply_fault_layer`; PIL M-FAULTCFG-1),
  not the `m::pil::impl::fault` internals.



## D10 — Centralized exception→`LSTATUS` mapping in the shim (M-FAULTCFG-3)

Wiring the fault layer (D9) into a scenario the *sample client* observes exposed
a gap in the shim's error translation. The fault decorator raises the `m::`
exception categories that mirror the real platform's statuses (`m::access_denied`,
`m::out_of_resources`, `m::sharing_violation`, `m::already_exists`,
`m::not_supported`, `m::not_found`) — all subclasses of `m::runtime_error`, **not**
`std::system_error`. The registry entry points in `mwinreg.cpp` each caught only
`std::system_error` (decoding a Win32/HRESULT code), so a fault-raised exception
would have escaped the `extern "C"` boundary and terminated the process instead of
being reported to the caller as an `LSTATUS`.

Decisions:

- **One mapping site.** Rather than scatter per-type `catch` clauses across the
  ~30 entry points, a single helper `registry_exception_to_lstatus()` performs the
  translation. It is called from within a `catch (...)` and rethrows the active
  exception to match its dynamic type, mapping each recognized category to its
  Win32 status and rethrowing anything unrecognized (so unrelated exceptions
  propagate exactly as before). Every entry point's catch block now funnels
  through this helper, replacing the previously duplicated `std::system_error`
  blocks.

- **Category→status table.** `m::not_found`→`ERROR_FILE_NOT_FOUND`,
  `m::access_denied`→`ERROR_ACCESS_DENIED`,
  `m::sharing_violation`→`ERROR_SHARING_VIOLATION`,
  `m::already_exists`→`ERROR_ALREADY_EXISTS`,
  `m::out_of_resources`→`ERROR_NOT_ENOUGH_MEMORY`,
  `m::not_supported`→`ERROR_NOT_SUPPORTED`, and (newly handled)
  `m::invalid_parameter`→`ERROR_INVALID_PARAMETER`. The last closes a pre-existing
  latent escape: `M_VALIDATE_PARAMETER` throws `m::invalid_parameter`, which the
  old `std::system_error`-only catches did not handle.

- **Function-specific overrides win.** A few query/enumeration entry points still
  carry their own `catch (m::not_found const&)` ahead of the catch-all, because
  they map "not found" to a *function-specific* status (e.g. `ERROR_NO_MORE_ITEMS`
  for value enumeration) rather than the generic `ERROR_FILE_NOT_FOUND`. The
  specific handler runs first; the centralized helper covers everything else.

This is the mono-repo "fix the layer" response: the gap was a real shim defect
surfaced by the fault scenario, so it was fixed where it lives (`mwinreg.cpp`)
rather than worked around in the test.

Note (see D12): this centralized helper translates **exceptions / `std::error_code`s**,
which are cross-cutting facts carried by the thrown object independent of which verb
raised them. It is emphatically **not** a disposition mapper — PIL `disposition` values
are owned per-verb and are never translated by shared code.

## D11 — Win32 filesystem API surface: handling inventory (M-FS-SHIM)

The Win32 filesystem surface is **much** broader than the registry surface — well over
two hundred entry points once the `A`/`W`, `Ex`, `Transacted`, and `2` variants are
counted. We do **not** shim all of them. This decision records the complete inventory and,
for every API, the deliberate disposition: what we redirect into the PIL filesystem
surface, what we forward untouched, and what we explicitly do not handle and why.

### Disposition codes

| Code | Meaning |
|---|---|
| **S** | **Shim now** (M-FS-SHIM) — redirect into PIL `ifilesystem` via the session. Isolated (passthrough / buffered / redirecting / logging / fault) per `.pilcfg`. |
| **S/ns** | **Shim now, namespace + metadata only** — the name, node kind, attributes, and timestamps are isolated; **byte content is not** (PIL D14). For passthrough the real content still flows; for buffered the content gap is the deferred PIL M-FS-STREAMS work. |
| **H** | **Handle-translating (alias mandatory).** The API consumes a file handle, and our handle could be one we minted — so the entry point **must** be aliased even though its *payload* may be deferred. It resolves the pseudo-handle to its backing object, then either forwards the payload to the real OS handle (passthrough) or serves/defers it from the content model (buffered). See the handle-translation invariant below. The payload disposition is given in the Notes (e.g. "content deferred (D14)"). |
| **D** | **Deferred** — a planned future milestone owns the *payload semantics* (file/stream **content**, alternate data streams, memory-mapped views, hard/symbolic links, volume enumeration). Not in M-FS-SHIM. (Where a deferred API also consumes our handle, it is **H** for the alias and **D** for the payload.) |
| **F** | **Forward** — intentionally passed straight to the real OS; not isolated. Reserved for APIs that take **no handle of ours** and either carry no path we choose to redirect, or whose isolation has near-zero value (process-global state, temp/system-directory probes, volume/disk geometry). A `.pilcfg` may still observe it through logging if we choose to wrap it later. |
| **X** | **Out of scope / not shimmed** — EFS raw backup, or non-namespace handle types (pipes, mailslots, completion ports). We do not alias these; genuine calls reach the real API unchanged. |

The default-build / default-test guarantee is unaffected: everything here is link-time
alias + runtime session redirection (D8), never a new build dependency.

### The handle-translation invariant

`CreateFile*` (and every other open/create entry point) returns a handle **we minted** via
the `handle_table` — a value with our recognizable bit pattern, **not** a real OS `HANDLE`.
That is deliberate: minting uniformly (even in passthrough) is what lets the isolation
envelope see, log, fault, or buffer *every* subsequent operation regardless of mode.

The direct consequence is a hard rule:

> **Any Win32 API that consumes a file handle must be aliased and must translate our
> pseudo-handle to its backing object before doing anything else.** There is no disposition
> under which a handle-consuming API may be left un-aliased while our handles can reach it —
> a genuine `ReadFile(ourHandle, …)` handed to the real `::ReadFile` would be given a value
> that is not a kernel handle and would fail with `ERROR_INVALID_HANDLE`.

So even content APIs we are *deferring* (`ReadFile`, `WriteFile`, `SetFilePointer`, …) are
still **shimmed now** for the handle-translation layer: in passthrough they resolve the
pseudo-handle to the real OS handle the session opened and forward the byte operation to it;
only the *buffered/virtual content model* is deferred (D14 / M-FS-STREAMS). These rows are
marked **H**, not **D** — the alias is mandatory, the payload is what's deferred. The same
applies to handle-based metadata, locking, durability, and `GetFileType`.

### Dusty-deck coverage

These techniques target **legacy ("dusty deck") code bases**, so we cannot assume callers use
the modern APIs. The old file primitives — `OpenFile`/`_lopen`/`_lcreat`/`_lread`/`_lwrite`/
`_llseek`/`_lclose`, the `LZ*` expansion family, and even the deprecated NTFS **Transacted**
entry points — are therefore **covered**, not dismissed. Most are thin historical wrappers
over the same namespace and handle model, so we alias them onto the same PIL verbs and the
same `handle_table`: a legacy `HFILE` we return is one of our minted values, and `_lread`/
`_lclose`/`LZRead`/`LZClose` translate it exactly like `ReadFile`/`CloseHandle` do. Transacted
variants are aliased too — we map them onto their non-transacted PIL op and **ignore the
transaction handle** (under a buffered/redirecting overlay the transaction is moot), which is
strictly safer than letting an un-aliased `CreateFileTransacted` reach the real volume and
defeat isolation. The only legacy things left **X** are those with no namespace identity to
isolate (EFS raw backup, non-file handle types).

### Open / create

| API (A/W unless noted) | Disposition | Notes |
|---|---|---|
| `CreateFileA` / `CreateFileW` | **S/ns** | Core open/create. Maps `dwCreationDisposition` / `dwDesiredAccess` / `dwShareMode` onto PIL `create_file` vs `open_file`; returns an interned handle (M-FS-SHIM-4). Content R/W deferred (D14). |
| `CreateFile2` | **S/ns** | Win8+ form taking `CREATEFILE2_EXTENDED_PARAMETERS`. Same mapping; lower priority than `CreateFileW`. |
| `ReOpenFile` | **H** | Re-derives a handle from an existing handle. Resolve our pseudo-handle, re-open the backing object with the new access/share, mint a fresh pseudo-handle. |
| `OpenFile` (legacy `OFSTRUCT`) | **S/ns** | Dusty-deck 16-bit-era open. Aliased: redirect the path, mint an `HFILE` from the same `handle_table`; the `_l*` family below translates it. `OF_*` flags mapped onto creation disposition. |
| `_lopen` / `_lcreat` | **S/ns** | Legacy open/create. Same path + handle treatment as `OpenFile`. |
| `CreateFileTransactedA` / `…W` | **S/ns** | TxF — deprecated, but dusty-deck code calls it. Aliased onto the non-transacted PIL open; the transaction handle is **ignored** (moot under a buffered/redirecting overlay). Mapping it is safer than letting it reach the real volume and break isolation. |

### Read / write / content & positioning

> All rows here consume a handle, so all are **H** (handle translation mandatory — see the
> invariant above). The **content** payload is the deferred PIL M-FS-STREAMS tier (D14): in
> passthrough the translated real handle serves real bytes; the buffered/virtual content
> model is what's deferred.

| API | Disposition | Notes |
|---|---|---|
| `ReadFile` / `ReadFileEx` / `ReadFileScatter` | **H** | Translate pseudo→real handle, forward the read (passthrough). Buffered content deferred (D14). |
| `WriteFile` / `WriteFileEx` / `WriteFileGather` | **H** | As reads. Buffered content deferred. |
| `_lread` / `_lwrite` / `_hread` / `_hwrite` | **H** | Legacy R/W over an `HFILE` we minted. Same translate-then-forward as `ReadFile`. |
| `SetFilePointer` / `SetFilePointerEx` / `_llseek` | **H** | Translate handle, forward the seek (passthrough). Virtual positioning deferred with the content tier. |
| `GetFileSize` / `GetFileSizeEx` | **H** | Size is metadata, but the call is handle-based: translate, then serve from `query_information`. |
| `SetEndOfFile` / `SetFileValidData` | **H** | Translate handle; allocation/EOF mutation forwarded (passthrough), buffered form deferred. |
| `FlushFileBuffers` | **H** | Translate handle, forward the durability barrier to the real handle. |
| `LockFile` / `LockFileEx` / `UnlockFile` / `UnlockFileEx` | **H** | Byte-range locks: translate handle, forward. No namespace effect, but the handle is ours so the alias is mandatory. |

### Delete / move / copy / replace

| API | Disposition | Notes |
|---|---|---|
| `DeleteFileA` / `DeleteFileW` | **S** | → PIL `remove_entry`. |
| `MoveFileA` / `MoveFileW` | **S** | → PIL `rename_entry`. |
| `MoveFileExA` / `MoveFileExW` | **S** | Adds `MOVEFILE_*` flags (replace-existing, copy-allowed, write-through, delay-until-reboot). Replace/copy honored; `DELAY_UNTIL_REBOOT` is best-effort/forward. |
| `MoveFileWithProgressA` / `…W` | **S** | As `MoveFileEx`; the progress callback is ignored under isolation (no physical copy). |
| `CopyFileA` / `CopyFileW` | **S/ns** | Namespace copy (create dest node). True byte copy depends on the content tier (D14); buffered copy of content is deferred. |
| `CopyFileExA` / `…W` / `CopyFile2` | **S/ns** | As `CopyFile`; progress/cancel callbacks ignored under isolation. |
| `ReplaceFileA` / `ReplaceFileW` | **S/ns** | Atomic replace = namespace re-key + backup node; content semantics deferred. |
| `MoveFileTransacted*` / `CopyFileTransacted*` | **S/ns** | TxF — deprecated, but dusty-deck code calls it. Aliased onto the non-transacted move/copy; transaction handle ignored (D11 invariant). |

### Attributes / metadata / info

| API | Disposition | Notes |
|---|---|---|
| `GetFileAttributesA` / `GetFileAttributesW` | **S** | → `query_information` (attributes). |
| `GetFileAttributesExA` / `…W` | **S** | Returns `WIN32_FILE_ATTRIBUTE_DATA` (attrs + timestamps + size) — all metadata PIL holds. |
| `SetFileAttributesA` / `…W` | **S** | → metadata mutation on the node. |
| `GetFileInformationByHandle` | **H** | Handle-based metadata read; translate the pseudo-handle, then serve the metadata. |
| `GetFileInformationByHandleEx` | **H** | Translate handle; many `FILE_INFO_BY_HANDLE_CLASS` classes — metadata classes served, content/stream/IO classes deferred or forwarded by class. |
| `SetFileInformationByHandle` | **H** | Translate handle; metadata classes (basic, rename, disposition) served; allocation/EOF classes deferred. |
| `GetFileTime` / `SetFileTime` | **H** | Handle-based timestamp metadata: translate handle, then serve/mutate. |
| `GetFileType` | **H** | Translate handle; our interned disk handles report `FILE_TYPE_DISK`. (Cannot forward an un-translated pseudo-handle to the real `::GetFileType`.) |
| `GetFinalPathNameByHandleA` / `…W` | **H** | Translate handle, resolve back to a path; under redirecting must map private→public. |
| `GetCompressedFileSizeA` / `…W` | **F** | On-disk compressed size — physical detail PIL does not model; forward. |
| `GetBinaryTypeA` / `…W` | **F** | Inspects an executable's image; forward. |
| `Get/SetFileAttributesTransacted*` | **S** | TxF — deprecated, but dusty-deck code calls it. Aliased onto the non-transacted attribute op; transaction handle ignored (D11 invariant). |

### Directory create / remove

| API | Disposition | Notes |
|---|---|---|
| `CreateDirectoryA` / `CreateDirectoryW` | **S** | → PIL `create_directory`. |
| `CreateDirectoryExA` / `…W` | **S** | Template-dir variant; template attributes best-effort. |
| `RemoveDirectoryA` / `RemoveDirectoryW` | **S** | → PIL `remove_entry` (directory). |
| `CreateDirectoryTransacted*` / `RemoveDirectoryTransacted*` | **S** | TxF — deprecated, but dusty-deck code calls it. Aliased onto the non-transacted directory op; transaction handle ignored (D11 invariant). |

### Enumeration / search / name resolution

| API | Disposition | Notes |
|---|---|---|
| `FindFirstFileA` / `FindFirstFileW` | **S** | → `enumerate_entries`; interns enumeration state; fills `WIN32_FIND_DATA` (M-FS-SHIM-5). |
| `FindFirstFileExA` / `…W` | **S** | Adds info-level / search-op / flags (large-fetch, case-sensitive); flags honored where PIL can. |
| `FindNextFileA` / `FindNextFileW` | **S** | Advances the interned cursor; `ERROR_NO_MORE_FILES` at end. |
| `FindClose` | **S** | Releases the interned enumeration state (distinct from `CloseHandle` — find handles are their own namespace). |
| `FindFirstFileNameW` / `FindNextFileNameW` | **D** | Hard-link enumeration — depends on the deferred links model. |
| `FindFirstStreamW` / `FindNextStreamW` | **D** | Alternate-data-stream enumeration — the deferred PIL M-FS-STREAMS tier 1. |
| `FindFirst*Transacted*` | **S** | TxF — deprecated, but dusty-deck code calls it. Aliased onto the non-transacted enumeration; transaction handle ignored (D11 invariant). |
| `SearchPathA` / `SearchPathW` | **S/ns** | Resolves a name against a search path; redirect each probed directory. |
| `GetFullPathNameA` / `…W` | **S** | Pure path canonicalization; route through PIL `file_path` (D11 of PIL) so `..`/separators match the isolated view. |
| `GetLongPathNameA` / `…W` | **S/ns** | Requires the namespace to resolve short→long; served from the isolated namespace. |
| `GetShortPathNameA` / `…W` | **F** | 8.3 short names are a volume feature we do not synthesize; forward. |
| `GetLongPathNameTransacted*` | **S/ns** | TxF — deprecated, but dusty-deck code calls it. Aliased onto the non-transacted resolution; transaction handle ignored (D11 invariant). |

### Hard links / symbolic links / reparse

| API | Disposition | Notes |
|---|---|---|
| `CreateHardLinkA` / `…W` | **D** | Links model is deferred (a node reachable by multiple names). |
| `CreateSymbolicLinkA` / `…W` | **D** | Reparse/symlink modeling deferred. |
| `CreateHardLink/SymbolicLinkTransacted*` | **D** | TxF — deprecated; aliased onto the non-transacted (deferred) link op, transaction handle ignored (D11 invariant). |
| `DeviceIoControl` (FSCTL reparse, etc.) | **H** | Takes a handle, so the alias is mandatory: translate the pseudo-handle, then forward the `IOCTL`/`FSCTL`. Only specific FSCTLs are filesystem-relevant; revisit individually if a scenario needs one isolated. |

### Current directory / well-known paths / temp

| API | Disposition | Notes |
|---|---|---|
| `GetCurrentDirectoryA` / `…W` | **F** | Process CWD — a process-global, not a namespace store. Forward (a future mode could virtualize it). |
| `SetCurrentDirectoryA` / `…W` | **F** | As above; forward. |
| `GetTempPathA` / `…W`, `GetTempPath2A` / `…W` | **F** | Probes env vars; low isolation value; forward. |
| `GetTempFileNameA` / `…W` | **S/ns** | Mints + creates a name in a directory — that directory is redirectable, so the created node lands in the isolated view. |
| `GetWindowsDirectoryA` / `…W`, `GetSystemDirectoryA` / `…W`, `GetSystemWindowsDirectory*` | **F** | Well-known system roots; forward (and intentionally so — HWC itself resolves `system32\inetsrv` this way, D-HWC-3). |
| `AreFileApisANSI` / `SetFileApisToANSI` / `SetFileApisToOEM` | **F** | Process-global A-API codepage toggle; forward (our `*A` shims honor CP_ACP regardless). |
| `NeedCurrentDirectoryForExePathA` / `…W`, `SetSearchPathMode`, `CheckNameLegalDOS8Dot3` | **F** | Policy/name-legality probes; forward. |

### Volumes / drives / mount points

| API | Disposition | Notes |
|---|---|---|
| `GetLogicalDrives`, `GetLogicalDriveStringsA` / `…W` | **F** | Enumerates real volumes; PIL roots are open-ended (PIL D10) but we do not synthesize a fake drive list in M-FS-SHIM. Forward; candidate for a later "virtual volume set." |
| `GetDriveTypeA` / `…W` | **F** | Forward. |
| `GetDiskFreeSpaceA` / `…W`, `GetDiskFreeSpaceExA` / `…W`, `GetDiskSpaceInformationW` | **F** | Physical capacity — not modeled; forward. |
| `GetVolumeInformationA` / `…W`, `GetVolumeInformationByHandleW` | **F** | Volume label/FS/serial; forward. |
| `GetVolumePathNameA` / `…W`, `GetVolumePathNamesForVolumeNameA` / `…W`, `GetVolumeNameForVolumeMountPointA` / `…W` | **F** | Volume↔path mapping; forward. |
| `SetVolumeLabel*`, `SetVolumeMountPoint*`, `DeleteVolumeMountPoint*` | **F** | Mutates real volume config; forward (never silently redirect a mount-point change). |
| `FindFirstVolumeA/W` … `FindVolumeClose`, `FindFirst/NextVolumeMountPoint*` | **D** | Volume enumeration — deferred with the virtual-volume idea. |
| `QueryDosDeviceA` / `…W`, `DefineDosDeviceA` / `…W` | **F** | DOS-device namespace; forward. |

### Memory-mapped files

| API | Disposition | Notes |
|---|---|---|
| `CreateFileMappingA` / `…W`, `CreateFileMapping2`, `CreateFileMappingNuma*` | **D** | Mapped views expose **content** by pointer — depends on the deferred content tier; a buffered file cannot back a real mapping. |
| `OpenFileMappingA` / `…W` | **D** | As above. |
| `MapViewOfFile` / `…Ex` / `…2` / `…3` / `…FromApp`, `UnmapViewOfFile*`, `FlushViewOfFile` | **D** | Deferred with mapping/content. |

### Change notification

| API | Disposition | Notes |
|---|---|---|
| `ReadDirectoryChangesW` / `ReadDirectoryChangesExW` | **S** | Already modeled by the PIL filesystem **monitor** (PIL D15). The shim routes these into `ifilesystem::monitor()`. |
| `FindFirstChangeNotificationA` / `…W`, `FindNextChangeNotification`, `FindCloseChangeNotification` | **S/ns** | Coarser event-only notification; map onto the monitor surface (no per-change detail). |

### Encryption / compression-stream / backup (specialized)

| API | Disposition | Notes |
|---|---|---|
| `EncryptFileA` / `…W`, `DecryptFile*`, `FileEncryptionStatus*`, `AddUsersToEncryptedFile`, … | **X** | EFS policy on real files; not modeled. Not aliased. |
| `OpenEncryptedFileRaw*`, `Read/WriteEncryptedFileRaw`, `CloseEncryptedFileRaw` | **X** | EFS raw backup stream; out of scope. |
| `BackupRead` / `BackupSeek` / `BackupWrite` | **H** | Whole-file structured stream (incl. ADS) over a handle: translate the pseudo-handle, forward (passthrough). The structured/ADS *content* belongs to the deferred stream tier. |
| `LZOpenFile*`, `GetExpandedName*` | **S/ns** | Dusty-deck LZ (`lz32`) expansion. Aliased: redirect the path, mint an `HFILE` from the `handle_table`. |
| `LZRead`, `LZSeek`, `LZClose`, `LZCopy`, `LZInit` | **H** | Operate on an LZ handle we minted: translate, then forward the decompression to the real handle (passthrough). Buffered content deferred. |
| `Wow64DisableWow64FsRedirection` / `…Revert` / `Wow64EnableWow64FsRedirection` | **F** | WOW64 redirection toggle — a *different* OS redirection mechanism; forward and leave alone. |

### Non-filesystem handle types (explicitly excluded)

| API family | Disposition | Notes |
|---|---|---|
| Named pipes (`CreateNamedPipe*`, `ConnectNamedPipe`, …), mailslots (`CreateMailslot*`) | **X** | Use file *handles* but are not the file *namespace*; out of scope for the filesystem surface (a future IPC surface could own them). |
| I/O completion ports (`CreateIoCompletionPort`, `GetQueuedCompletionStatus*`, `PostQueuedCompletionStatus`) | **X** | Generic async I/O plumbing, not filesystem; forward/untouched. |

### Consequences for the alias set and `CloseHandle`

The **S** / **S/ns** / **H** rows are the ones whose names go into `mwin32.def` so the
`mwin32_alias` object (D8) redirects genuine client calls. **H** rows in particular are
**not optional**: because we mint pseudo-handles, every handle-consuming entry point must be
aliased or a genuine call would hand a non-handle to the real API (the handle-translation
invariant above). **F** and **X** rows are *not* aliased — genuine calls reach the real API.

Two cross-cutting handle entry points sit at the seam:

- `CloseHandle`: file handles use it (there is no `RegCloseKey` analogue), so `mCloseHandle`
  must inspect the handle and release only values minted by our `handle_table`, forwarding
  every other handle to the real `::CloseHandle` (M-FS-SHIM-6). It is the only shim *broader*
  than its registry analogue, which is why `CloseHandle` aliasing is opt-in and carries that
  caveat in the checklist.
- `DuplicateHandle`: **H** — when the source is one of ours it must mint a second
  `handle_table` entry referring to the same backing object (honoring close-source semantics);
  when the source is foreign it forwards to the real `::DuplicateHandle`.

This is the same seam that makes the handle-translation invariant load-bearing: the moment we
chose to mint uniformly (so the isolation envelope sees every op), we committed to aliasing
*every* handle-consuming API, legacy `HFILE`/`LZ` handles included.

## D12 — Flags and dispositions belong to a *verb*, not an interface; their mapping is never shared

Each PIL operation declares its **own** `*_flags`, `*_result_code`, and `*_result_flags`
enums *inside the abstract virtual member function that owns that operation* — e.g.
`idirectory::open_directory_flags` / `open_directory_result_code` are distinct types from
`idirectory::open_file_flags` / `open_file_result_code`, even though both happen to spell a
`not_found` member today. A `disposition<open_directory_result_code, …>` and a
`disposition<open_file_result_code, …>` are **different, non-interchangeable types**. The
vocabulary is owned by the verb, not by `idirectory` and not by "the filesystem surface."

The consequence, stated as bluntly as possible so a future contributor does not re-derive the
wrong conclusion:

- **There is no shared flags→PIL or disposition→Win32 mapping table, and there must never be
  one.** The superficial resemblance between, say, `open_directory`'s "not found" and
  `open_file`'s "not found" is a *recurring happenstance*, not a *structural* similarity. It
  does not license shared code. Writing a single "map a filesystem disposition to a Win32
  last-error" function forces a union of result-code vocabularies, and the instant one verb
  gains a code that is meaningless for another (it will), the shared function is already wrong.
  Each call site interprets the specific disposition of the specific verb it just called.

- **The only cross-verb invariant is the trivial gate**, which is a *validation*, not a
  *mapping*: when a caller passes `flags == 0` (no behavior opted into), the returned
  disposition must be empty (no contractual non-error outcome can be produced). A code is only
  ever produced when the matching flag opted into it (e.g. `open_*_result_code::not_found`
  appears only when `tolerate_not_found` was passed). A generic assert of that gate is
  legitimate; a generic translator of the *values* is not.

- **What *is* legitimately centralized is orthogonal:** the exception / `std::error_code` →
  Win32 translation in `win32_error_mapping.h` (D10). That is shareable precisely *because it
  is not disposition mapping*. An exception's category (`m::not_found`, `m::access_denied`, …)
  and a `std::system_error`'s Win32 code are cross-cutting facts carried by the *thrown object*,
  independent of which verb raised them; the `error_code` channel and the `disposition` channel
  are deliberately separate (see `filesystem_interfaces.h`). Sharing the exception mapper does
  **not** imply, and must not grow into, a shared disposition mapper.

How `mwinfile.cpp` already honors this: the metadata/namespace entry points never read a
disposition *code*. `query_path_metadata` distinguishes present/absent purely through the
`tolerate_not_found` contract — a null returned pointer with no `ec` means "not that kind of
node here" — and reports genuine failures through the `error_code` channel, which the single
exception mapper then translates. No per-verb result code is ever inspected, so there is
nothing to centralize and no temptation to start.

This decision schedules no new work: it is a guardrail constraining how the remaining
M-FS-SHIM items (and any future PIL-backed shim) are written.

## D13 — Handle-based metadata: reads are served, writes are an accepted no-op (M-FS-HANDLE-META)

The handle-consuming metadata APIs (`GetFileInformationByHandle`,
`GetFileInformationByHandleEx`, `GetFileSize`/`Ex`, `GetFileTime`/`SetFileTime`, `GetFileType`,
`GetFinalPathNameByHandle`, `SetFileInformationByHandle`) translate the shim-minted pseudo-handle
to its backing `m::pil::ifile` (D11 invariant) and serve every **read** from
`ifile::query_information`. Size is always the metadata size, never a content length (D14).

The PIL filesystem surface this milestone exposes exactly one metadata verb — `query_information`
(a read). There is **no** metadata-write verb on `ifile`. The metadata-mutation entry points
(`SetFileTime`, and the metadata `FILE_INFO_BY_HANDLE_CLASS` classes of
`SetFileInformationByHandle`: basic, rename, disposition) therefore resolve the handle, validate
the request, and report **success without persisting any change**. This is the same
accept-and-ignore stance the shim already takes for behavior PIL cannot model under isolation
(e.g. `MoveFileEx` ignoring `dwFlags`, the progress callbacks of the copy family). It is a
deliberate, documented no-op, not an oversight:

- **Why not fail?** A caller that sets a timestamp or a basic-info attribute expects success;
  failing would break dusty-deck code that ignores the result anyway, and there is no honest
  Win32 error meaning "your write was understood but this provider keeps no writable metadata."
- **Why not extend PIL now?** The milestone is explicitly scoped "no PIL change." Adding a
  metadata-write verb is a cross-component PIL change deferred to a future milestone; when it
  lands, only these entry points' implementations change, never their specification.
- **The boundary is sharp.** Classes whose backing data is *byte content or on-disk allocation*
  (`FileAllocationInfo`, `FileEndOfFileInfo`, stream/compression/IO classes) are **not** accepted:
  they report `ERROR_NOT_SUPPORTED` (the deferred-content error, M-FS-CONTENT) rather than a
  silent success, because pretending to resize content would be a correctness lie, whereas
  accepting an unpersisted metadata write is a tolerable fidelity gap.

`GetFinalPathNameByHandle` returns the **public** path the caller opened with, taken from the
path stored in the handle's `file_handle_state` at `mCreateFile` time. The redirecting decorator
maps public→private internally, so storing the caller's path is exactly the private→public
mapping the API must report — with no reverse-mapping support required from the provider. GUID /
NT volume forms have no PIL analogue and report `ERROR_NOT_SUPPORTED`; the DOS form renders the
extended-length `\\?\` escape and the volume-less form renders a drive-relative path.


## D14 — Path-based copy / replace / temp-file / path-resolution: namespace-only, metadata writes are no-ops, replace is single-root (M-FS-COPY)

> Numbering note: this is **mwin32 decision D14**. Earlier bare "(D14)" / "PIL D14"
> citations in this file point at the *PIL* M-FS-STREAMS decision (a different
> component's numbering space), not at this decision.

The remaining **path-based** namespace/metadata APIs the inventory (D11) marks S / S/ns —
the copy family (`CopyFileW/A`, `CopyFileExW/A`, `CopyFile2`), `ReplaceFileW/A`,
`CreateDirectoryExW/A`, `GetTempFileNameW/A`, `SetFileAttributesW/A`, and the path-resolution
family (`GetFullPathNameW/A`, `GetLongPathNameW/A`, `SearchPathW/A`) — are implemented entirely
on the metadata/namespace verbs PIL already exposes. No handle content is involved, so the
milestone needs no PIL change.

- **Copies are namespace-only.** A copy verifies the source exists (a missing source fails
  `ERROR_FILE_NOT_FOUND`; a directory source fails `ERROR_ACCESS_DENIED`) and, subject to the
  fail-if-exists check, creates the destination as a fresh **empty** node via `create_file`.
  Byte content is the deferred PIL M-FS-STREAMS tier, so the copy reproduces the *name and node
  kind*, never the bytes. The progress / cancel callbacks of `CopyFileEx` / `CopyFile2` and the
  copy-flag bits other than `COPY_FILE_FAIL_IF_EXISTS` are accepted and ignored — the same
  accept-and-ignore stance D13 documents for unmodellable behavior. `CopyFile2` reports the same
  outcomes as an `HRESULT` (`HRESULT_FROM_WIN32`).

- **Replace is a single-root namespace re-key.** `ReplaceFile` requires the replaced and
  replacement nodes to exist and, when a backup is requested, re-keys the replaced node onto the
  backup name (removing any prior backup first) before re-keying the replacement node onto the
  replaced name; without a backup the replaced node is removed outright. Because PIL's
  `rename_entry` operates within one root, all three names must share a root — a cross-root
  request fails `ERROR_NOT_SAME_DEVICE` rather than silently copying across roots. The replace
  flags and the reserved parameters are ignored.

- **`GetTempFileName` mints deterministically.** With `uUnique == 0` the name is chosen by
  scanning the 16-bit space upward from 1 and creating the first unused node empty — deterministic
  under isolation rather than seeded from the system clock — so repeated calls return distinct,
  freshly created names. With `uUnique != 0` the name is formed from the low 16 bits and **no**
  node is created, matching the genuine API. `CreateDirectoryEx` ignores the template directory
  (there is no metadata to clone) and otherwise creates the directory like `CreateDirectory`.
  `SetFileAttributes` verifies the target exists (missing → `ERROR_FILE_NOT_FOUND`) and then
  accepts and discards the attribute mask: PIL exposes no metadata-write verb this milestone, so
  the set is the same documented no-op as D13's handle-based metadata writes.

- **Path resolution routes through PIL `file_path` (D11).** `GetFullPathName` canonicalizes with
  `lexically_normal(path_surface::windows)` and emits the result under the shared Win32 path-name
  length contract (chars-excluding-null on success; required-size-including-null when the buffer is
  absent or too small); `lpFilePart` is pointed at the final component within the caller buffer, or
  null when the path has no distinct file component. There is no current directory under isolation,
  so a relative input is normalized lexically rather than rooted at a CWD. `GetLongPathName`
  additionally requires the path to exist (a missing path fails `ERROR_FILE_NOT_FOUND`) and returns
  the canonical form unchanged — there is no short/long distinction to expand. `SearchPath` walks
  the semicolon-separated `lpPath` directories (a default extension is appended only when the name
  carries none) and returns the canonical path of the first existing match; a `NULL` `lpPath`
  selects the real default search order, which has no meaning under isolation and so fails
  `ERROR_FILE_NOT_FOUND`.

## D15 — Change notification: shimmed onto the PIL monitor; live-provider-only; directory handles and the FindFirst* event model (M-FS-NOTIFY)

The Win32 change-notification family — the detailed `ReadDirectoryChangesW` /
`ReadDirectoryChangesExW` pair and the coarse `FindFirstChangeNotification` family — is realized on
the **already-complete** PIL filesystem monitor (`ifilesystem::monitor()`, PIL D15). No PIL change
was required. The `FILE_NOTIFY_CHANGE_*` mask and `bWatchSubtree` flag project one-to-one onto
`register_watch_flags`, and the monitor's detailed `(kind, entry_name)` records project back onto
the `FILE_NOTIFY_INFORMATION` action codes; the two mappings are exact inverses of the direct
provider's own filter/action mapping, so the Win32 semantics survive the round-trip across the PIL
surface.

- **Notifications fire only under a live provider, not buffering.** Whether a watch observes
  anything is decided by the *provider*, not by the shim. The direct (passthrough) provider
  implements the watch with a genuine `ReadDirectoryChangesW` against the real directory and reports
  real mutations; the **buffered** provider models a sealed snapshot and its `register_watch` is
  unimplemented (it throws `M_NOT_IMPLEMENTED`). Consequently the notification APIs require a
  non-buffered configuration backed by a real directory — which is why the M-FS-NOTIFY-3 test runs
  under a *passthrough* `.pilcfg` watching a real scratch directory under the OS temp path, unlike
  the buffered+redirecting `.pilcfg` the copy / metadata suites use.

- **A notification directory handle carries only a path.** `ReadDirectoryChangesW` needs a
  *directory* handle, which the genuine API obtains with `FILE_FLAG_BACKUP_SEMANTICS`. The shim's
  `CreateFile` honors that flag by validating the directory exists and interning a
  `file_handle_state` whose backing `ifile` is **null** and whose only meaningful field is the
  caller's path; the per-handle watch (and the PIL monitor token it owns) is installed lazily on the
  first `ReadDirectoryChangesW` and torn down by RAII when the handle closes. A consequence and
  current limitation: the handle-based *metadata* APIs (D13) assume a non-null backing `ifile`, so
  calling them on a backup-semantics directory handle is out of scope for this milestone.

- **Async delivery is bridged to the Win32 read shapes by a per-handle queue.** The monitor calls
  back on a threadpool thread; the shim funnels each record into a per-handle queue guarded by a
  mutex. A synchronous `ReadDirectoryChangesW` (NULL `lpOverlapped`) blocks on a condition variable
  until a record is queued and then decodes the queue; an overlapped call either drains immediately
  (records already queued) or records the read as *pending* so the next callback fills the buffer,
  sets `*lpBytesReturned`, and signals the `OVERLAPPED` event. Completion-routine (APC) delivery is
  not modeled — `lpCompletionRoutine` is ignored; an event-bearing `OVERLAPPED` is the supported
  asynchronous form. The extended class `ReadDirectoryChangesExW` rejects anything but
  `ReadDirectoryNotifyInformation` with `ERROR_INVALID_PARAMETER`: the extended records carry
  timestamps and sizes the PIL surface does not deliver, so silently returning basic records would
  misrepresent the contract.

- **`FindFirstChangeNotification` returns a real OS event, not a minted handle.** Because the shim
  does **not** intercept `WaitForSingleObject`, the handle this family returns must be genuinely
  OS-waitable. So `FindFirstChangeNotification` creates a real manual-reset Win32 event, registers an
  event-only watch whose callback signals it (discarding the per-change detail this family does not
  report), and records the owning context in a process-wide side registry keyed by the event handle
  — it cannot live in `handle_table`, which mints pseudo-handles outside the OS namespace.
  `FindNextChangeNotification` re-arms by resetting the event; `FindCloseChangeNotification` looks the
  context up, removes it, and lets it die *after* releasing the registry lock so the watch's
  cancellation (which may block on an in-flight callback) never contends on that lock. Teardown order
  inside the context is load-bearing: the token is released first (quiescing callbacks), then the
  sink, and only then is the event closed.

- **Redirection and the live monitor path-shape reconciliation (implemented).** The redirecting
  decorator keys on the *relative* directory name (e.g. `mwin32_copy_pub`), whereas a live watch
  needs a *root-qualified* directory path to open. The PIL `fs_redirector::try_map` now handles this
  by suffix-matching on the relative portion of rooted paths: given `C:\temp\xxx\pub_prefix\child`,
  it strips leading components from the relative path until it finds `pub_prefix` in the redirection
  table, then reconstructs `C:\temp\xxx\priv_prefix\child`. This enables a redirected-directory
  notification test (M-FS-NOTIFY-REDIR milestone).

## D-SDK — Publishable mwin32 SDK: layout, CPack component, multi-arch, pipeline assembly (M-SDK)

The `mwin32` shim is shipped to external consumers as a standalone, downloadable
**SDK** — distinct from the full `m` developer release. A consumer wants exactly
three things: the public headers, the shim binaries for the architecture they
target, and enough scaffolding (CMake package + examples + guide) to link and run.
The SDK packages precisely that and nothing else. The user-facing contract is the
guide at [`docs/mwin32-sdk-guide.md`](docs/mwin32-sdk-guide.md); this decision
records the *producer* side.

Decisions:

- **One package, per-architecture binary subtrees.** The SDK is a single artifact
  with shared, architecture-neutral `include/`, `lib/cmake/m/`, `docs/`, and
  `examples/` trees, plus one `x64/{bin,lib}` and one `arm64/{bin,lib}` subtree for
  the architecture-specific `m_mwin32.dll` / `m_mwin32.lib` / `mwin32_alias.lib`.
  Rationale: headers and the CMake package are identical across architectures, so
  duplicating them per-arch would only invite drift; the binaries are the only
  thing that genuinely differs. The exact layout is normative and is mirrored in §3
  of the guide — the two must be kept in sync.

- **A dedicated CPack component, not the full-`m` zip.** The SDK install rules are
  tagged `COMPONENT mwin32_sdk` so `cpack -D CPACK_COMPONENTS_ALL=mwin32_sdk`
  produces the SDK in isolation, independent of the broader `m` release artifact.
  This keeps the SDK small (no unrelated `m` libraries) and lets the SDK and the
  full release be cut from the same build without entangling their contents.

- **Bundled examples build against the *installed* package.** The three
  [`sample/`](sample) clients ship as sources under `examples/` together with a
  generated top-level `examples/CMakeLists.txt` that does
  `find_package(m CONFIG REQUIRED)` and links `m::mwin32_alias` — i.e. they consume
  the SDK exactly as an external user would, **not** via the in-tree CMake targets.
  This makes the examples a live, out-of-tree proof that the packaged CMake config
  actually works; an example that only built in-tree would not catch a broken
  install/export.

- **Multi-arch is assembled by merging two single-arch installs.** CMake configures
  one architecture per build tree, so the SDK is produced by building+installing the
  `mwin32_sdk` component for x64 and for ARM64 separately, then merging the two
  install trees into the §3 layout (shared trees taken once, binaries placed under
  their `x64/` / `arm64/` subtree). The merge is the single place that knows the
  cross-arch layout. Corollary fix: the alias **import-library** generation in
  [`CMakeLists.txt`](CMakeLists.txt) currently hard-codes `/machine:x64`; it must
  follow the active target architecture so the ARM64 build emits a correct ARM64
  alias import lib.

- **Assembly happens in the GitHub release pipeline, on the existing tag trigger.**
  The SDK is built and published by GitHub Actions on the same `v*` tag push that
  cuts the full release (extending
  [`.github/workflows/release.yml`](../../../../.github/workflows/release.yml) or a
  sibling workflow): a matrix builds both architectures, the merge runs, and the
  result is attached to the GitHub Release as `mwin32-sdk-<tag>.zip` alongside the
  full-`m` zip. There is no manual/local assembly path in the supported flow — the
  pipeline is the source of truth — though a single-arch `cpack` remains usable
  locally for verification.

## D16 — DLL-client / FreeLibrary lifetime: process rundown vs. live unload

`m_mwin32.dll` mints watch handles (D15) and interns them in the process-wide
`g_handles` table. Because `CloseHandle` is marked `; noalias` in `mwin32.def`, an
ordinary client's `::CloseHandle` of a minted handle never reaches `mCloseHandle`,
so minted directory-watch handles — each owning a PIL monitor token whose teardown
blocks on threadpool wait/timer callbacks (D15) — can still be live in `g_handles`
when the host process ends. The two ways a process can end are fundamentally
different and must be handled separately.

### Process termination (the `g_handles` teardown hazard)

When the process exits, the C runtime tears the DLL down in a fixed order:
`_DllMainCRTStartup` calls the user `DllMain(DLL_PROCESS_DETACH)` **first**, then
runs the static-destructor / `atexit` table — which includes the `g_handles`
destructor. By the time `g_handles`'s destructor runs, the OS loader has already
terminated every other thread in the process. Driving a watch token's normal
teardown there (`WaitForThreadpool*Callbacks`) would wait forever on worker
threads that no longer exist (a hang), and any late trace would reach
infrastructure that is mid-teardown (the dangling-monitor failure recorded in the
tracing component's `DESIGN-NOTES.md` D1).

Decisions:

- **A minimal `DllMain` records the cause of detach.** mwin32 keeps the standard
  CRT entry point and adds its own `DllMain` (canonical MSVC form, no
  `extern "C"`). On `DLL_PROCESS_DETACH` it inspects `lpReserved`: per the Microsoft
  contract, `lpReserved != NULL` means the process is terminating, `== NULL` means a
  `FreeLibrary` unload while the process keeps running. On the terminating case it
  sets an anonymous-namespace `g_process_terminating` flag in `handle_table.cpp`.
  The body does nothing else — under loader lock only a trivial store is safe.

- **`g_handles` leaks itself on termination.** `handle_table`'s destructor checks
  `g_process_terminating`; when set, it moves its map into a heap allocation and
  abandons it (`new std::map<...>(std::move(m_table))`) rather than destroying the
  entries. The watch tokens — and their threadpool waits — are never torn down; the
  OS reclaims the address space. On a normal (non-terminating) destruction the table
  tears down as usual.

- **Why a flag, not a direct query, drives the leak.** The leak decision lives in
  mwin32 (layer above PIL), so it is keyed on mwin32's own `DllMain` signal rather
  than reaching down into a lower layer. This keeps the layering one-directional:
  PIL never reads an mwin32 symbol.

### `FreeLibrary` unload (process lives on)

When a DLL client deliberately unloads the provider with `FreeLibrary` while the
process continues, the address space is **not** being reclaimed: leaking the watch
tokens would leak real OS resources for the remaining life of the process, and the
threadpool threads are still alive, so a normal quiesce **is** safe and correct.
This is the opposite of the termination case, and `lpReserved == NULL` is exactly
how `DllMain` tells them apart.

A client that unloads the provider mid-process is therefore responsible for
quiescing its outstanding watches *before* `FreeLibrary` (closing the minted
handles, which on the redirecting path runs the token's normal teardown). To make
that determination easy and uniform, the redirecting library exposes a reusable
rundown helper — `m::pil::impl::redirecting::process_rundown_in_progress()`
([`src/libraries/pil/src/redirecting/rundown.h`](../../../libraries/pil/src/redirecting/rundown.h)) —
backed on Windows by ntdll's `RtlDllShutdownInProgress` (resolved once by name; it
is not in the public SDK headers). It reports true **only** during process
termination, not for a single `FreeLibrary`. The redirecting monitor-token wrapper
(`filesystem_monitor_change_notification_wrapper`) consults it in its destructor: on
process rundown it *releases* (leaks) its underlying direct token to skip the unsafe
threadpool teardown; on a live unload it lets the underlying token tear down
normally. This gives the redirecting layer the same leak-on-terminate semantics as
`g_handles`, but expressed where the token actually lives and reusable by any PIL
consumer — not just mwin32.

Cross-reference: the tracing component's process-lifetime monitor is itself a
deliberately leaked singleton for the same family of reasons (its `DESIGN-NOTES.md`
D1); together these ensure that nothing mwin32 touches during late shutdown
dereferences an already-freed object.

## D17 — `.pilcfg` host-path members support `%VAR%` environment-variable expansion

A `.pilcfg` is intended to be checked in alongside the code it configures and then
run on whatever machine clones the repository. The host-path members it carries
(`persisted_state`, `capture_snapshot`, `diagnostic_log`, `fault_script`, and the
webcore `materialization_dir` / `fault_script`) therefore cannot be hard-coded
absolute paths — they must resolve to per-machine locations such as a temp or
profile directory.

Specified behavior (owned by us, not inherited from any dependency): when
`parse_pilcfg` reads a member that denotes a **host filesystem path**, it expands
Windows `%VAR%` tokens against the current process environment. A `%NAME%` token is
replaced by the value of environment variable `NAME`; an **undefined** token is left
verbatim; a value containing no `%` token is returned unchanged. Expansion happens at
parse time, so the returned `pilcfg` already carries resolved paths.

Decisions:

- **Only host-path members are expanded.** Logical namespace identifiers —
  redirection `from`/`to` keys and webcore `endpoints` `public`/`private` — are taken
  **literally** and are never expanded, so a legitimate `%` inside a key or endpoint
  is never disturbed. The two classes of string are deliberately treated differently
  because one names a location on the host and the other names a node in the virtual
  namespace.

- **`ExpandEnvironmentStringsW` is the chosen implementation.** Its `%VAR%` syntax and
  its "undefined token left verbatim" contract match our specification, so it is used
  to realize the behavior. The specification is what we guarantee; the API is merely
  how we achieve it. On any API failure the literal member value is returned (the
  member is never dropped).

- **No new schema members.** Expansion is a property of *how existing path members are
  interpreted*, not a new toggle, so a checked-in `.pilcfg` needs no opt-in. Files
  that happen to contain no `%` are wholly unaffected.

The helper (`expand_environment_path`, `pilcfg.cpp`) and its per-member application
are unit-tested in `test_pilcfg.cpp` (`PilcfgExpand.*`), including the negative cases
that redirection keys and webcore endpoints are preserved verbatim.


## D18 — Production live-edge contract wiring is a webcore decorator (M-HWC-CONTRACTCFG-7)

`.pilcfg` contract bindings (D-HWC-8) are wired onto a *running* engine, not merely
held as standalone documents. `webcore_config_platform::get_webcore` wraps the
configured engine in a **contract-wiring webcore decorator**
(`make_contract_wiring_webcore`, `webcore_config_platform.cpp`). The decorator's
`activate` forwards to the underlying webcore, then — on the activated instance's
`synthetic_http_edge()` — does two things, reusing the bound-contract set from
CONTRACTCFG-3 and the validate/drive tally shape from CONTRACTCFG-6:

1. **Validate-mode** documents are registered as `crossing_observer`s. Every
   autonomous request/response that crosses the live edge is contract-checked
   (`validate_request` / `validate_response`) and tallied into a
   `live_contract_diagnostics` side record. This is a pure side diagnostic (D6): the
   engine's behavior is never altered by validation, and an observer never feeds back
   into the request path.
2. **Drive-mode** documents are driven against the activated engine via
   `drive_contract(*document, m::pil::make_engine_submit(edge, timeout))`, generating
   the spec's synthesized traffic through the same public submit seam.

Decisions:

- **Decorator, not a platform rewrite.** The wiring lives entirely in the webcore
  decorator returned by `get_webcore`; the rest of the platform stack forwards
  unchanged. When no contracts are bound the decorator is a transparent pass-through,
  and when the activated instance exposes no synthetic edge (`synthetic_http_edge() ==
  nullptr`, e.g. the null engine) wiring is a tolerant no-op. The returned instance
  owns the registered observers / wiring for the activation's lifetime.

- **Per-instance validate serialization.** Crossing observers may fire concurrently on
  the engine's servicing thread(s), so each activation guards its validate-document
  access with a per-instance mutex (`m_validate_mutex`); the diagnostics record is
  guarded by its own mutex. Drive and validate documents are distinct document objects,
  so the two modes never contend on the same document.

- **Engine-agnostic seam; only IIS is unexercised in CI.** The decorator wires onto
  the public `isynthetic_http_edge` seam, which is engine-agnostic. The CI integration
  test (`ContractCfgIntegration.LiveEdgeWiresValidateAndDriveOverInProcessEngine`)
  drives the **same** decorator over an in-process engine
  (`m::pil::make_in_process_webcore`). The production real-`hwebcore` path is the
  *same* decorator with the intercepting webcore (synthetic mode) over a real
  `hwebcore.dll` as the config-selected engine. The only element not exercised in CI
  is IIS / `hwebcore.dll` itself — an acknowledged limit (PIL D-HWC-11), not a gap in
  the wiring, which is identical on both paths.

## D19 — Wire-capture is transport-agnostic; topology is a sample/harness axis, never a capture concern

The mwin32 wire-capture feature (`CHECKLIST-wirecapture.md`) tees raw HTTP bytes off
the Winsock `send`/`recv` shims, reassembles them into messages (HTTP/1.1
`Content-Length` framing, v1), and feeds a capture sink that either records (→ the PIL
contract recorder emits an OpenAPI YAML spec) or validates (→ `validate_request` /
`validate_response` tallies). None of these three stages — tee, reassembler, sink —
learns the socket address family, the resolved peer, or whether the peer is in another
process. That independence is a deliberate design property, not an accident, and it is
the headline result the demo proves.

Decisions:

- **Two orthogonal axes define the demo's transport matrix.** *Address path*:
  arbitrary DNS name (`getaddrinfo` → connect), IPv4 loopback (`127.0.0.1`), IPv6
  loopback (`::1`), optionally AF_UNIX (IP-less). *Process model*: in-process two
  threads over a real loopback socket (the deterministic CI harness — bind port `0`,
  read back the ephemeral port), the in-process synthetic edge (no Winsock at all,
  bytes handed straight to the reassembler — unifies with the existing D-HWC-11
  synthetic-edge contract tests), and optionally two separate processes (the faithful
  hand-run demo, kept non-gating because child-process orchestration is flaky in CI).

- **`getaddrinfo` is not teed.** Name resolution is not byte I/O on the data socket, so
  the DNS variant changes only how the client *obtains* an address; the per-socket tee
  is byte-for-byte identical to the literal-address variants. The DNS topology exists
  to prove resolution doesn't disturb the tee, not to capture resolver traffic.

- **Address-family blindness.** The IPv4 and IPv6 variants produce identical HTTP byte
  streams; only the `sockaddr` passed to `connect`/`bind` differs. The reassembler and
  sink must never branch on family — the matrix test asserts the derived spec and the
  violation tallies are equal across IPv4, IPv6, DNS, and synthetic transports.

- **Topology never enters the capture `.pilcfg` schema.** The capture config carries
  only `mode` (record | validate), the spec path, and an optional host/endpoint
  filter — all transport-agnostic. The *server* sample selects its bind family
  (`--family ipv4|ipv6|dual`) and the *client* sample selects its connect target
  (`--target dns:<host>:<port> | ipv4:<port> | ipv6:<port>`) by CLI argument. Pushing
  topology into the capture layer would couple a pure side-channel (D6) to transport
  details it has no business knowing and would break the family-blindness property
  above.

## D20 — Winsock shims tee transferred bytes per socket; lowercase shim names; synchronous-only v1 (WC-1)

The wire-capture feature's first layer is a set of Winsock interception shims
(`msocket`, `mconnect`, `maccept`, `msend`, `mrecv`, `mclosesocket`, `mWSASend`,
`mWSARecv`; [`src/mwinsock.cpp`](src/mwinsock.cpp), [`include/m/mwin32/mwinsock.h`](include/m/mwin32/mwinsock.h)).
Each mirrors the genuine ws2_32 signature, forwards to the real ws2_32 export, and
tees the bytes ws2_32 reports actually transferred into a per-socket capture buffer
owned by the process-wide session ([`src/session.cpp`](src/session.cpp)). The teed
bytes feed the reassembler / sink built in later milestones.

Decisions:

- **The tee is a pure side-channel (D6), driven by the *reported* transfer count.**
  The shim calls the genuine function first, then tees exactly the `send`/`recv`
  return value (or the `WSASend`/`WSARecv` `lpNumberOfBytes*` out-parameter) of bytes —
  never the requested length. A short transfer therefore tees only what crossed the
  wire, so the capture is byte-exact even under partial sends/reads. The shim's return
  value and any caller buffer are never altered. Outbound (`send`/`WSASend`) and
  inbound (`recv`/`WSARecv`) streams are captured separately, keyed by the raw `SOCKET`
  value; `closesocket` releases the socket's buffers.

- **Synchronous transfers only in v1.** `WSASend`/`WSARecv` are teed only on
  synchronous completion (`lpOverlapped == NULL` and an immediate success); an
  overlapped (asynchronous) call reports its byte count through the `OVERLAPPED`
  later, which the shim does not observe, so overlapped completions are forwarded
  faithfully but **not** teed. This is a documented v1 limitation, consistent with the
  HTTP/1.1-`Content-Length`-only framing scope (D19) — the demo's samples use blocking
  sockets, so the synchronous path is the exercised one.

- **The alias name-shape validation was loosened for lowercase genuine names.** The
  Winsock genuine names (`socket`, `connect`, `send`, ...) are lowercase, so the shim
  names are `m` + a lowercase letter. The alias generator's export-name validation
  regex ([`generate_mwin32_alias.cmake`](generate_mwin32_alias.cmake)) was widened from
  `^m([A-Z]|_)` to `^m([A-Za-z]|_)` to admit them. The mechanical Win32-name derivation
  (strip the leading `m`: `msocket` → `socket`) is unchanged, and no existing export is
  affected — the change only *accepts* a previously rejected shape, at the cost of a
  slightly weaker typo guard.

- **The shim never recurses into itself.** The link-time alias redirects a *client's*
  Win32 calls into these shims, but `m_mwin32.dll` does not link the alias object, so
  the unqualified `::socket` / `::send` / ... calls inside the shims bind to ws2_32
  normally.

- **Cross-module singleton note (testing).** The shims run inside `m_mwin32.dll`, which
  owns one session singleton; a test that links `m_mwin32_internal` directly compiles a
  *second*, independent copy of that singleton. A capture written by a DLL shim is
  therefore not observable through the in-test session accessors. The WC-1 tests respect
  this split: the capture mechanism is unit-tested directly against the in-test session
  (`MWinSockTee.*`), and the shims are exercised end-to-end over a real IPv4 loopback
  connection asserting only byte-identical passthrough (`MWinSockPassthrough` /
  `LoopbackFixture`), which needs no capture readback. Observing a DLL-shim capture
  end-to-end is deferred to the sink-seam and integration milestones.

## D21 — HTTP/1.1 reassembler is pure, family-blind, Content-Length-framed (WC-2)

The teed byte stream (D20) is turned into complete HTTP/1.1 messages by a pure
reassembler ([`src/http_reassembler.h`](src/http_reassembler.h),
[`src/http_reassembler.cpp`](src/http_reassembler.cpp)) that lives in
`m_mwin32_internal` so it is unit-testable without the DLL or any socket.

Decisions:

- **No transport dependency (family-blind, D19).** The reassembler never sees a
  `SOCKET`, an address family, or any OS facility — it consumes raw bytes via
  `feed(const void*, size_t)` and emits `http_request` / `http_response` values via
  `next(out)`. The identical code therefore reassembles IPv4, IPv6, DNS-resolved,
  and in-process loopback captures, which is what lets WC-11 assert the derived
  contract is identical across transports.

- **Single shared framing engine.** Requests and responses frame identically (start
  line, CRLF-separated headers, blank line, `Content-Length` body); only the
  start-line grammar differs. `http_framing` owns the byte buffer and the
  headers/body state machine and yields a generic `http_frame`; the request and
  response reassemblers add only start-line parsing (request-line vs status-line) on
  top. This keeps the partial-read and pipelining logic in exactly one place.

- **`Content-Length` framing only; absence means empty body (v1).** Consistent with
  D19/D20, a message with no `Content-Length` is framed as having a zero-length body
  and the following bytes begin the next message; `Transfer-Encoding: chunked` is not
  decoded. The body is length-delimited opaque bytes, so NULs and embedded CRLFs in
  the body never confuse framing (covered by `BodyWithBinaryAndEmbeddedCrlf`).

- **Tolerant parsing at the edges.** Header names are matched case-insensitively
  (ASCII) and values are OWS-trimmed per RFC 9110; a `Content-Length` value that is
  empty or contains a non-digit is treated as zero rather than raising (the capture
  path is a side-channel and must never throw into a shim). A header line with no
  colon is skipped. These are pragmatic v1 choices for a diagnostic capture, not a
  conformant HTTP parser.

- **Incremental, allocation-simple buffering.** `feed` appends to a byte vector and
  `next` consumes a complete message by erasing its bytes from the front. Message
  sizes in the demo are small, so the simple erase-from-front buffer is preferred
  over a ring buffer; if large bodies ever matter this is the obvious place to add a
  read offset.

## D22 — Capture sink seam: observational consumer that pairs crossings FIFO (WC-3)

The reassembled messages (D21) are delivered to a `capture_sink`
([`src/capture_sink.h`](src/capture_sink.h),
[`src/capture_sink.cpp`](src/capture_sink.cpp)) — the seam where later milestones
plug in a tally, a PIL recorder (`record` mode), or a PIL validator (`validate`
mode). The seam lives in `m_mwin32_internal` and is pure logic.

Decisions:

- **The seam is the consuming end of an observational tee (D6).** A
  `connection_capture` owns a request reassembler and a response reassembler and is
  fed the bytes that were *also* sent to the genuine socket. It returns nothing the
  shim hands back to the caller and holds no socket, so by construction it cannot
  alter, delay, or block the wire. The `ByteForwardingIsUnaffectedBySink` test makes
  this concrete: it models the shim's tee (genuine bytes to a `wire` buffer, a copy
  to the seam) and asserts the wire is byte-identical whether a no-op sink or a
  tallying sink is attached.

- **Crossings are paired FIFO per connection.** `connection_capture` notifies the
  sink `on_request` / `on_response` as each message completes, and emits
  `on_crossing` whenever an unpaired request and an unpaired response are both
  available, popping both from their queues. FIFO order is correct for HTTP/1.1
  keep-alive (responses return in request order), and the queues mean either stream
  may drain first — a response that arrives before its request simply waits.

- **Default-no-op callbacks.** `capture_sink`'s three hooks default to doing
  nothing so a sink that only wants crossings (or only wants per-message events)
  overrides just what it needs. `tallying_capture_sink` is the WC-3 concrete sink:
  it counts requests, responses, and crossings with per-method and per-status
  breakdowns, holds no reference to the wire, and never throws into the capture
  path.

## D23 — `.pilcfg` capture schema: mode + spec + optional host filter (WC-4)

The wire-capture feature is driven from the existing `.pilcfg` document via a new
optional top-level `capture` object, parsed into `pilcfg::capture_config`
([`src/pilcfg.h`](src/pilcfg.h), [`src/pilcfg.cpp`](src/pilcfg.cpp)). Absent
`capture` yields `std::nullopt`; a present-but-malformed shape throws, consistent
with the rest of the parser.

Decisions:

- **Two modes, one spec path, opposite roles.** `mode` is required and is exactly
  `"record"` or `"validate"` (any other value throws). `spec` is the contract
  document path and is required and non-empty. The same member names the *output*
  in `record` mode and the *input* in `validate` mode — there is one spec path, and
  the mode decides whether it is written or read. This keeps the schema minimal and
  makes the record→validate round-trip a one-word edit.

- **`spec` is a host path; `host` is a logical value.** `spec` is `%VAR%`-expanded
  via the same `expand_environment_path` path as every other host-filesystem member
  (D-PILCFG host-path rule), so a checked-in config can reference per-machine
  locations. The optional `host` member is a Host-header filter taken **literally**
  (never `%VAR%`-expanded) and stored as UTF-8 `std::string` so it can be compared
  directly against the bytes of an HTTP `Host:` header without re-encoding. Absent
  `host` is an empty string meaning "capture all".

- **`"drive"` is not a capture mode.** The webcore-contract schema (D-HWC-8) uses
  `mode = validate | drive`; capture uses `mode = record | validate`. They are
  separate namespaces — `"drive"` is rejected by the capture parser — so the two
  features cannot be confused by a config author who knows one of them.

## D24 — Capture sinks bind the seam to PIL: record derives, validate checks (WC-5)

The two capture modes (D23) are realized as two concrete `capture_sink`s
([`src/capture_sink.h`](src/capture_sink.h),
[`src/capture_sink.cpp`](src/capture_sink.cpp)) that consume the paired
`http_crossing` (D22) and route it to the matching PIL surface. Both live in
`m_mwin32_internal` (which already links `m_pil`) and are pure logic.

Decisions:

- **Both sinks act on the paired crossing, not the per-message hooks.** A
  `recording_capture_sink` forwards a crossing to a PIL `ihttp_contract_recorder`
  (`observe_request` + `observe_response`), and a `validating_capture_sink` runs a
  crossing through a PIL `ihttp_contract_document` (`validate_request` +
  `validate_response`). Pairing first is required by PIL's API — `validate_response`
  and `observe_response` are keyed on the request's method + path — and it keeps
  request and response analysis together. A request without a matching response (a
  half-open connection) is simply never analyzed; the demo's streams always pair.

- **The wire vocabularies are converted, not adapted.** The reassembler's
  `http_header{name,value}` (D21) becomes PIL's `http_header` (a name/value pair)
  by a flat copy, and the body `vector<uint8_t>` is passed as a `span`. The request
  target is passed verbatim — PIL strips the query string and uses the observed
  path as the operation's path template, so no URL parsing happens on the mwin32
  side. This is why `record` mode collapses `/search?q=a` and `/search?q=b` into a
  single operation.

- **Violations are tallied per direction; operational errors are not violations.**
  `validating_capture_sink` keeps a `validation_tally` with separate request- and
  response-direction counts. A truthy validate disposition is a violation in that
  direction; a clean disposition is a conforming check; an error reported through
  the `std::error_code` channel (e.g. a malformed spec) is neither checked nor a
  violation. This mirrors PIL's own `drive_contract` accounting so the derive→detect
  demo reports both sides consistently.

- **The sinks never touch the wire (D6).** Like every sink, these read only the
  observational copy and return nothing the shim feeds back, so attaching either
  one leaves the bytes on the genuine socket byte-identical (asserted by
  `ValidatingCaptureSink.DoesNotAlterTheWire`).

## D25 — HTTP server sample: ordinary raw-Winsock app, topology and fault are runtime switches (WC-6)

The demo's server
([`sample/mwin32_http_server_sample.cpp`](sample/mwin32_http_server_sample.cpp))
is a plain raw-Winsock HTTP/1.1 server that includes only the Winsock headers and
calls the genuine `socket`/`bind`/`listen`/`accept`/`recv`/`send`/`closesocket`.
It is made observable solely by its CMake target linking `mwin32_alias` (D8) — the
same redirection mechanism the registry/filesystem samples use, now exercising the
Winsock shims (WC-1).

Decisions:

- **No mwin32 awareness in the sample.** The server carries no `#include` of any
  mwin32 header and no `.pilcfg` knowledge. Capture is enabled (and its mode
  chosen) entirely by the sidecar config the harness drops next to the binary; with
  no sidecar the shim is a transparent passthrough and the server is an ordinary
  app. This keeps the sample an honest demonstration that *unmodified* Winsock code
  becomes capturable purely at link time.

- **Topology is a runtime switch, never baked into capture (D19).** `--family
  ipv4|ipv6|dual` picks the bind family (`AF_INET` loopback, `AF_INET6` loopback,
  or `AF_INET6` with `IPV6_V6ONLY` cleared) and `--port N` picks the port. `--port
  0` requests an ephemeral port, which the server reads back with `getsockname` and
  echoes as `port=<N>` on stdout so a harness can connect without a fixed port.
  Because the reassembler and sinks are family-blind, the same server drives every
  transport in the WC-11 matrix.

- **The fault is a contract violation, not a transport fault.** `--fault` (or env
  `MWIN32_SAMPLE_FAULT=1`) makes `GET /health` answer `{"status":0}` instead of
  `{"status":"ok"}` — a well-framed 200 response whose body fails the derived JSON
  schema (status typed as a number). The connection is never broken; the response
  still carries a correct `Content-Length`. This is exactly what validate mode must
  catch as a server→client violation while traffic completes (WC-10).

- **A control endpoint gives the harness a clean stop.** `GET /shutdown` answers
  `{"bye":true}` and then ends the accept loop, so a test can terminate the server
  deterministically rather than killing the process. The server frames requests by
  `Content-Length` only (the v1 reassembler scope, D21) and keeps connections alive
  so request/response pairing (D22) is exercised.

## D26 — HTTP client sample: ordinary raw-Winsock app, connect target and fault are runtime switches (WC-7)

The demo's client
([`sample/mwin32_http_client_sample.cpp`](sample/mwin32_http_client_sample.cpp))
is the mirror of the server sample (D25): a plain raw-Winsock HTTP/1.1 client that
includes only the Winsock headers and calls the genuine
`getaddrinfo`/`socket`/`connect`/`send`/`recv`/`closesocket`. It is made
observable solely by its CMake target linking `mwin32_alias` (D8). It drives the
server's endpoints — `GET /health`, `POST /widgets`, and (only with `--shutdown`)
`GET /shutdown` — and reports each response status as `tag=value` so a harness can
assert on it.

Decisions:

- **No mwin32 awareness in the sample.** Like the server (D25), the client carries
  no mwin32 `#include` and no `.pilcfg` knowledge. Whether its traffic is captured
  (and in which mode) is decided entirely by the sidecar config a harness drops
  next to the binary; with no sidecar the shim is a transparent passthrough.

- **The connect target is a runtime switch, never baked into capture (D19).**
  `--target` selects how the peer is reached: `dns:<host>:<port>` runs the host
  name through `getaddrinfo` (the DNS-resolved path of the WC-11 matrix);
  `ipv4:<port>` connects to literal `127.0.0.1`; `ipv6:<port>` connects to literal
  `::1`. The literal targets build the `sockaddr` directly with no name
  resolution, mirroring the server's bind families. `getaddrinfo` is deliberately
  *not* one of the teed entry points — name resolution is out of band; only the
  `send`/`recv` byte stream is captured — so the three targets produce the same
  observed HTTP traffic, which is the transport-independence result WC-11 asserts.

- **The fault is a contract violation, not a transport fault.** `--fault` (or env
  `MWIN32_SAMPLE_FAULT=1`) makes `POST /widgets` send `{"name":123}` (name typed as
  a number, `size` omitted) instead of the conforming `{"name":"widget","size":3}`
  — a well-framed request with a correct `Content-Length` whose body fails the
  derived request schema. The server still answers 201 and the connection is never
  broken; this is exactly what validate mode must catch as a client→server
  violation while traffic completes (WC-10). The fault switch shares the same
  env-var contract as the server so a single harness setting can fault either side.

- **`--shutdown` is opt-in so the harness owns server lifetime.** By default the
  client drives only `/health` and `/widgets`, leaving the server running for
  further exchanges (e.g. a second client, or a faulted run after a clean one). The
  harness passes `--shutdown` on the last run to ask the server to exit cleanly via
  its control endpoint rather than killing the process.

## D27 — HTTP samples ship in the SDK examples; a reference contract cross-checks the derived spec (WC-8)

The wire-capture sample pair (D25 server, D26 client) is wired into the build and
the SDK package exactly like the existing redirect samples, and a hand-authored
reference OpenAPI document
([`sample/reference-openapi.yaml`](sample/reference-openapi.yaml)) ships alongside
them.

Decisions:

- **The HTTP samples join the existing example layout, not a new one.** Both
  in-tree targets are ordinary `add_executable`s linking `mwin32_alias` plus
  `ws2_32` (the only addition over the registry/filesystem samples — the genuine
  socket entry points the alias redirects). Their sources are added to the
  `mwin32_sdk` install component's `examples/` set, and the out-of-tree
  [`sdk-examples-CMakeLists.txt`](cmake/sdk-examples-CMakeLists.txt) builds them
  against the installed `m::mwin32_alias` package in a second `foreach` that adds
  `ws2_32`. An external consumer therefore builds the HTTP demo the same way as
  every other example.

- **The reference contract is hand-authored to mirror the recorder's inference,
  not the wire bytes.** The YAML describes the two business endpoints (`GET
  /health` → 200, `POST /widgets` request + 201) with schemas written to match
  exactly what `infer_json_schema` produces from the sample bodies:
  `object`/`properties`/`required`, `string` for JSON strings and `integer` for
  whole numbers. This makes a *structural* comparison against the spec the recorder
  derives (WC-9) meaningful — the reference is the expected shape, the derived YAML
  is the observed shape, and the lifecycle test asserts they agree.

- **The control endpoint is deliberately absent from the contract.** `GET
  /shutdown` is a harness lifetime control, not part of the API, so it is omitted
  from the reference document; the client only exercises it on an opt-in
  `--shutdown` run that the derive phase excludes (D26). The reference therefore
  describes precisely the traffic the clean derive phase observes.

## D28 — In-process wire-capture harness and the derive phase (WC-9)

The integration tests
([`test/test_wirecapture_integration.cpp`](test/test_wirecapture_integration.cpp))
stand on a reusable harness
([`test/wirecapture_harness.h`](test/wirecapture_harness.h)) that runs the same
two-endpoint exchange (`GET /health`, `POST /widgets`) over any of four transports
and returns the request/response byte streams teed off the connection, ready to be
replayed through a `connection_capture` into the WC-5 sinks.

Decisions:

- **The harness observes at the client's wire boundary, not through the shim's
  session.** The mwin32 shims live in `m_mwin32.dll`, which owns its own capture
  session singleton; the test links `m_mwin32_internal` with a *distinct*
  singleton, so bytes teed by a DLL shim are not readable in-test (the standing
  split documented in `test_mwinsock.cpp`). The harness therefore tees the bytes
  itself — every byte the client sends is appended to the request stream and every
  byte it receives to the response stream — which is byte-for-byte the same
  observational copy the production tee makes (D6), just taken in the harness so
  the integration test is self-contained and deterministic. This exercises the
  reassembler, sinks, recorder, and validator end to end over genuine loopback
  framing (partial reads, keep-alive request/response pairing).

- **The transport is a four-way switch; content is invariant across it.** `ipv4`
  and `ipv6` bind a loopback listener on an OS-assigned ephemeral port (bind `0`,
  read back with `getsockname`) and run server and client on two threads; `dns`
  does the same but resolves `localhost` through `getaddrinfo` on both ends with
  the bound family pinned so the two agree; `synthetic` skips Winsock entirely and
  builds the two streams in process from the same routing logic. Because the
  reassembler and recorder key off message content, not transport, every transport
  produces identical streams — the transport-independence result WC-11 asserts,
  proven early here for `ipv4` vs `synthetic` so the derive phase is self-contained.

- **Derive asserts the spec, not just its existence.** Phase 1 records a clean
  exchange through `recording_capture_sink`, then asserts `operation_count == 2`,
  that the emitted YAML names both endpoints and both statuses, and — crucially —
  that the derived YAML *loads cleanly* back through the live PIL contract provider
  (`make_platform_interface()->get_http_contract()->load`) and yields two
  synthesizable operations for `/health` and `/widgets`. The round-trip (derive →
  emit → load) is what makes the derived contract usable as the validation input
  for the detect phase (WC-10).

## D29 — Detect phase: faults caught in both directions, traffic intact (WC-10)

Phase 2 closes the lifecycle: the contract *derived* in phase 1 (D28) is loaded in
validate mode and run against a faulted exchange, asserting that a violation is
seen in each direction while the connection completes.

Decisions:

- **The validation input is the derived contract, not the hand-authored
  reference.** The detect test derives a clean spec over IPv4, loads it through the
  live provider, and validates against *that* — so the test proves the full
  capture→derive→load→validate loop rather than smuggling in an external oracle.
  The reference document (D27) remains the human-readable cross-check; the machine
  loop stands on its own derived artifact.

- **Both faults are injected at once and each lands in its own direction.** The
  faulted run sets `fault_request` and `fault_response` together: `POST /widgets`
  carries `{"name":123}` (a request-body schema violation, client→server) and `GET
  /health` answers `{"status":0}` (a response-body schema violation, server→client).
  Because the other endpoint in each crossing stays conforming, the tally records
  exactly one violation per direction (`request_violations >= 1`,
  `response_violations >= 1`) with both directions checked twice — proving the sink
  keys the response check on the request's method+path and tallies per direction.

- **Traffic completing is asserted, not assumed.** The test checks both byte
  streams are non-empty after the faulted run: the violations are contract
  failures surfaced only through the side-channel (D6), never transport breaks, so
  the two framed messages still flow. A companion clean-traffic test asserts zero
  violations against the same derived contract, the false-positive guard that makes
  the fault detection meaningful.

## D30 — Transport matrix: the lifecycle is transport-independent (WC-11)

The headline result of the demo is that wire capture is a property of the byte
stream, not the transport (D19). The matrix test proves it by running the whole
derive→detect lifecycle over every transport the harness supports and asserting
the outputs are identical.

Decisions:

- **One lifecycle helper, four transports.** `run_lifecycle(transport)` derives a
  clean contract, loads it, and validates a both-directions-faulted exchange over
  the *same* transport, returning the derived YAML and the violation tally. The
  test calls it for `ipv4`, `ipv6`, `dns`, and `synthetic` and asserts every
  derived spec is byte-identical to the IPv4 one and every tally is equal — the
  transport-independence result stated as a single equivalence across the matrix.

- **Equivalence is anchored to a non-trivial tally.** Asserting the four tallies
  are *equal* would be vacuously satisfied if they were all zero, so the test also
  asserts the shared tally caught a violation in each direction
  (`request_violations >= 1`, `response_violations >= 1`, both directions checked
  twice). Equal *and* non-trivial together make the equivalence meaningful.

- **IPv6 and DNS stay on loopback; the optional extras are out of scope.** `ipv6`
  binds `::1` and `dns` resolves `localhost` (which resolves to a loopback address)
  with the bound family pinned across both ends, so the matrix never leaves the
  machine and triggers no firewall prompt. AF_UNIX and a two-separate-process
  smoke were called out in the checklist as optional, non-gating extras and are
  deliberately not implemented — the in-process synthetic edge already covers the
  no-Winsock case, and the three socket transports cover the family/resolution
  axes that matter for the transport-independence claim.


