<!-- Copyright (c) Microsoft Corporation. -->

# mwin32 SDK — User's Guide

> Audience: C++ engineers who want to run existing Win32 code against an
> **isolated**, controllable view of the registry and filesystem — most often to
> make tests **hermetic** (no live machine state, fully reproducible) without
> rewriting the code under test.

---

## 1. What mwin32 is, and why it exists

`mwin32` is a Windows-only, drop-in replacement DLL (`m_mwin32.dll`) for a subset
of the Win32 API. Today it covers three families:

| Family | Example entry points | Count |
|---|---|---|
| **Registry** | `RegCreateKeyExW`, `RegSetValueExW`, `RegQueryValueExW`, `RegCloseKey`, … | 84 |
| **Filesystem** | `CreateFileW`, `FindFirstFileW`, `CopyFileExW`, `GetFileAttributesExW`, `ReadDirectoryChangesW`, … | ~110 |
| **Hostable Web Core** | `WebCoreActivate`, `WebCoreShutdown`, `WebCoreSetMetadata` | 3 |

Every shim entry point is named with a leading `m` (`mRegCreateKeyExW`,
`mCreateFileW`, `mWebCoreActivate`, …) and is a *thin* redirect into the `m`
package's **PIL** (Platform Isolation Library). PIL is a decorator stack, so the
same client code can run in one of several modes **chosen outside the program**:

- **passthrough** — calls flow straight through to the live Win32 API. Behaves
  exactly like the real OS; zero isolation.
- **logging** (`record_modifications`) — calls run live *and* every modification
  is recorded so it can be written out and inspected.
- **buffered** (`buffer_updates`) — modifications land in an in-memory overlay
  *away from the live system*. The program reads back what it wrote, but the live
  registry / filesystem is never touched. The overlay can be persisted to a
  snapshot file.
- **persisted replay** — the program runs entirely against a previously captured
  snapshot over a *null* backing OS, so it needs no live machine state at all.
- **redirecting** — a path/key prefix is rewritten to a private backing location.
- **fault injection** — selected calls fail with a chosen error, to exercise
  error paths deterministically.

### Why this matters for tests

A test that calls `RegSetValueExW` or `CreateFileW` directly normally has two bad
options: mock every OS call (invasive, and you stop testing the real call), or
touch the live machine (non-hermetic, order-dependent, leaves residue). `mwin32`
gives a third option: **run the unmodified code, but redirect its OS calls into an
isolated overlay** so the test is reproducible and leaves nothing behind.

### Mode selection — the `.pilcfg` sidecar

The active mode is **not** compiled into your program. At startup the shim looks
for a JSON file named `<executable>.pilcfg` next to the host `.exe`
(`GetModuleFileNameW(nullptr, …)`). If it is absent or malformed, the shim falls
back to **passthrough** and never breaks the host. If present, it selects the
stack:

```json
{ "buffer_updates": true }
```

```json
{
  "buffer_updates": true,
  "redirections": [ { "from": "mytest_pub", "to": "mytest_priv" } ]
}
```

```json
{ "persisted_state": "snapshot.xml" }
```

Schema members (all optional):

| Member | Type | Effect |
|---|---|---|
| `buffer_updates` | bool | Buffered overlay (writes isolated in memory). |
| `record_modifications` | bool | Logging layer (records modifications). |
| `redirections` | array of `{ "from", "to" }` | Rewrite a key/path prefix to a private backing location. |
| `persisted_state` | string | Run against a captured XML snapshot over a null OS. |
| `fault_script` | string | Reference to a fault-injection script (deterministic failures). |
| `webcore` | object | Hostable Web Core isolation options. |

Unknown members are ignored (forward-compatible). See the component
`DESIGN-NOTES.md` (D5, D7, D9) for the exact semantics.

---

## 2. Two ways to consume mwin32

### Option A — Link the alias object (no source changes)

If your code already calls the **genuine** Win32 names (`RegCreateKeyExW`,
`CreateFileW`, …) through `<windows.h>`, you can redirect it at **link time** with
no source edits by linking the `mwin32_alias` object library:

```cmake
target_link_libraries(my_client PRIVATE mwin32_alias)
```

`mwin32_alias` contains no logic. For every shim export it defines the matching
`__imp_<Win32Name>` import-address-table slot and points it at the `m*` shim, so a
`__declspec(dllimport)` call from `<windows.h>` lands in the shim instead of
advapi32 / kernel32. The slot set is generated from `mwin32.def`, so it can never
drift from the exports. Linking the object transitively brings in `m_mwin32.dll`.

**What the alias redirects, and what it cannot.** This is a deliberately shallow,
supported, link-time mechanism. It redirects the Win32 calls the client itself
*links*. It does **not** redirect:

- calls made through `GetProcAddress` / `LoadLibrary` (resolved at runtime);
- calls already compiled into a **third-party static library** that hard-references
  the system import slots;
- the advapi32 → kernelbase API-set layering beneath the public names.

When `advapi32.lib` / `kernel32.lib` is on the link line it wins any *plain*
(non-`dllimport`) reference; the `__imp_` slot is the reliable path, and real
`<windows.h>` clients always take it. Reaching the excluded cases requires a
runtime-interception (Detours) envelope, which is out of scope for the SDK.

### Option B — Call the `m*` shims directly

If you are writing new code (or can edit the code under test), include the shim
headers and call the `m*` names directly. This bypasses the alias entirely and has
none of the alias limitations above.

```cpp
#include <m/mwin32/mwinreg.h>   // mReg* registry shims
#include <m/mwin32/mwinfile.h>  // m* filesystem shims
#include <m/mwin32/mwinhwc.h>   // mWebCore* shims

HKEY    key = nullptr;
LSTATUS rc  = ::mRegCreateKeyExW(HKEY_CURRENT_USER, L"my\\subkey", 0, nullptr,
                                 REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE,
                                 nullptr, &key, nullptr);
```

The `m*` entry points reproduce the Win32 contracts (return codes, last-error
behavior, buffer-size negotiation) of the functions they shim.

---

## 3. Linking against the SDK

The SDK ships a CMake package. After installing/extracting it:

```cmake
find_package(m CONFIG REQUIRED)

add_executable(my_tool main.cpp)

# Option A — link-time redirection of genuine Win32 calls:
target_link_libraries(my_tool PRIVATE m::mwin32_alias)

# Option B — call the m* shims directly:
target_link_libraries(my_tool PRIVATE m::m_mwin32)
```

The SDK layout (per architecture) is:

```
mwin32-sdk/
  include/m/mwin32/      mwinreg.h, mwinfile.h, mwinhwc.h, mWindows.h
  x64/
    bin/m_mwin32.dll
    lib/m_mwin32.lib, mwin32_alias.lib
  arm64/
    bin/m_mwin32.dll
    lib/m_mwin32.lib, mwin32_alias.lib
  lib/cmake/m/           package config (find_package(m))
  examples/              the sample clients below, buildable as-is
  docs/mwin32-sdk-guide.md
```

`m_mwin32.dll` must be discoverable at run time (same directory as your `.exe`, or
on `PATH`). When you link via CMake targets, `$<TARGET_RUNTIME_DLLS:...>` can copy
it next to your executable automatically — see the examples' `CMakeLists.txt`.

---

## 4. Making a test hermetic — worked example

Goal: test a function that writes and reads a registry value, with **no effect on
the live registry** and **fully reproducible** results.

### 4.1 The code under test (unchanged)

This is ordinary Win32 code. It includes only `<windows.h>` and has no knowledge
of mwin32:

```cpp
// settings.cpp — production code, not modified for the test
#include <windows.h>

bool save_count(unsigned long count)
{
    HKEY    key = nullptr;
    LSTATUS rc  = ::RegCreateKeyExW(HKEY_CURRENT_USER, L"Acme\\App", 0, nullptr,
                                    REG_OPTION_NON_VOLATILE, KEY_WRITE,
                                    nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS) return false;
    rc = ::RegSetValueExW(key, L"count", 0, REG_DWORD,
                          reinterpret_cast<const BYTE*>(&count), sizeof(count));
    ::RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}
```

### 4.2 The test target — link the alias, generate a `.pilcfg`

```cmake
add_executable(test_settings test_settings.cpp settings.cpp)
target_link_libraries(test_settings PRIVATE mwin32_alias GTest::gtest_main)

# Hermetic: buffer every registry write into an in-memory overlay so the live
# registry is never touched and the test is reproducible.
add_custom_command(
    OUTPUT $<TARGET_FILE:test_settings>.pilcfg
    COMMAND ${CMAKE_COMMAND} -E echo {\"buffer_updates\": true} > $<TARGET_FILE:test_settings>.pilcfg
    VERBATIM)
add_custom_target(test_settings_pilcfg DEPENDS $<TARGET_FILE:test_settings>.pilcfg)
add_dependencies(test_settings test_settings_pilcfg)

# Copy m_mwin32.dll next to the test executable so it can launch.
add_custom_command(TARGET test_settings POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:test_settings> $<TARGET_FILE_DIR:test_settings>
    COMMAND_EXPAND_LISTS)
```

### 4.3 The test body

```cpp
// test_settings.cpp
#include <windows.h>
#include <gtest/gtest.h>

bool save_count(unsigned long count);   // from settings.cpp

TEST(Settings, SaveCountIsHermetic)
{
    // Runs against the buffered overlay selected by test_settings.exe.pilcfg.
    EXPECT_TRUE(save_count(7));

    // Read it back — the overlay returns exactly what was written...
    HKEY key = nullptr;
    ASSERT_EQ(ERROR_SUCCESS,
              ::RegOpenKeyExW(HKEY_CURRENT_USER, L"Acme\\App", 0, KEY_READ, &key));
    DWORD value = 0, cb = sizeof(value), type = 0;
    EXPECT_EQ(ERROR_SUCCESS,
              ::RegQueryValueExW(key, L"count", nullptr, &type,
                                 reinterpret_cast<BYTE*>(&value), &cb));
    EXPECT_EQ(7u, value);
    ::RegCloseKey(key);

    // ...and the LIVE registry under HKCU\Acme\App was never created.
}
```

Because the overlay is in memory and discarded at process exit, every run starts
clean — the defining property of a hermetic test. Note the buffered session is
process-wide and persists across tests in the same executable, so use distinct
key/value names per test (or split into separate test executables) if you need
full inter-test isolation.

### 4.4 Filesystem isolation

The filesystem family works the same way. For files, combine `buffer_updates`
with a `redirections` entry so a public path prefix is served from a private
backing location:

```json
{
  "buffer_updates": true,
  "redirections": [ { "from": "acme_pub", "to": "acme_priv" } ]
}
```

Code that calls `CreateFileW(L"acme_pub\\data.bin", …)` then reads and writes
through the isolated overlay rather than the real path. This is exactly how the
component's own `test_mwin32_fscopy` / `test_mwin32_fslegacy` suites stay
hermetic — see [`../test/CMakeLists.txt`](../test/CMakeLists.txt).

---

## 5. Capture once, replay forever

You can capture a real run's registry/filesystem effects into a snapshot, then
replay your program (or test) against that snapshot on a machine that has none of
the original state:

1. **Capture** — run under `{ "buffer_updates": true }`; the overlay is persisted
   to an XML snapshot.
2. **Replay** — run under `{ "persisted_state": "snapshot.xml" }`; reads and writes
   go entirely to the loaded snapshot over a *null* OS. Layer flags and
   redirections are ignored in this mode.

This makes it possible to reproduce a customer's environment-dependent bug in CI
without that environment.

---

## 6. Bundled examples

The SDK `examples/` folder contains buildable, runnable clients that link
`mwin32_alias` and demonstrate each surface end-to-end:

| Example | Demonstrates |
|---|---|
| `mwin32_sample_client.cpp` | Registry create/set/query/enum/delete under buffered, logging, and persisted-replay `.pilcfg`s. |
| `mwin32_fs_sample_client.cpp` | Filesystem create/find/copy/move under a redirecting overlay. |
| `mwin32_notify_sample_client.cpp` | Directory change notifications (`ReadDirectoryChangesW`) through the shim. |

Each example has a `.pilcfg` and copies `m_mwin32.dll` next to itself, so it can be
launched directly after building.

---

## 7. Reference

- **Component overview:** [`../COMPONENT.md`](../COMPONENT.md)
- **Design decisions:** [`../DESIGN-NOTES.md`](../DESIGN-NOTES.md) — D5 (`.pilcfg`),
  D6 (registry value ops), D7 (redirections + persisted state), D8 (alias object),
  D9 (fault scripts), D11+ (filesystem inventory).
- **PIL internals:** [`../../../../libraries/pil/DESIGN-NOTES.md`](../../../../libraries/pil/DESIGN-NOTES.md)

### Limitations at a glance

- Windows only; x64 and ARM64 binaries ship in the SDK.
- The alias object redirects only **link-time** Win32 references (see §2A).
- The shim covers the registry, filesystem, and Hostable Web Core families listed
  in §1 — not the entire Win32 surface.
- A missing/invalid `.pilcfg` always degrades to **passthrough**; it never breaks
  the host process.
