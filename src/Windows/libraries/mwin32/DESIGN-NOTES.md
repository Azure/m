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
