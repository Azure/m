// Copyright (c) Microsoft Corporation.
//
// MW5-6: link-proof for the Rust `windows-win32-shim` alias object.
//
// This translation unit is the C++ link-proof from the mwin32 C++ tree
// (`src/Windows/libraries/mwin32/test/test_mwin32_alias.cpp`), reused here with
// its GoogleTest harness replaced by a dependency-free `main` so it can be built
// and run by a standalone PowerShell recipe (`run-linkproof.ps1`) outside the
// CMake/vcpkg build. The Win32 call sequence and the redirection argument are
// otherwise unchanged.
//
// It deliberately includes NO shim headers. It calls the genuine Win32 registry
// entry points (RegCreateKeyExW / RegSetValueExW / RegQueryValueExW /
// RegCloseKey). The executable links the alias object emitted by `gen-alias-obj`
// (from `windows_win32_shim_aliases.ndjson`), whose `__imp_` slot definitions
// redirect those calls into the Rust shim cdylib (`windows_win32_shim.dll`, via
// its import library `windows_win32_shim.dll.lib`). With a buffered `.pilcfg`
// next to the executable, every write must land in the shim's in-memory overlay
// and never reach the live registry.
//
// Redirection is proven two ways:
//   (1) the value written through the redirected Win32 API reads back through the
//       redirected Win32 API (the buffered overlay captured it), and
//   (2) the REAL advapi32 entry points, obtained via GetProcAddress (which the
//       alias deliberately does not redirect), cannot find the write in the live
//       registry.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
    // Failure counter; any non-zero value fails the process (the harness keys on
    // the exit code). Replaces GoogleTest's assertion bookkeeping.
    int g_failures = 0;

    void
    report(bool ok, const char* what)
    {
        if (ok)
        {
            std::printf("  [ ok ] %s\n", what);
        }
        else
        {
            std::printf("  [FAIL] %s\n", what);
            ++g_failures;
        }
    }

    // A subkey name unique to this process+run so concurrent or repeated runs do
    // not collide, and so a stray live-registry entry (which would indicate a
    // redirection failure) is unambiguous. A single path component is used because
    // the buffered overlay's create_key operates one level at a time (it does not
    // auto-create intermediate keys the way live RegCreateKeyExW does).
    std::wstring
    unique_subkey()
    {
        std::wstring s = L"win32_shim_alias_test_";
        s += std::to_wstring(::GetCurrentProcessId());
        s += L'_';
        s += std::to_wstring(::GetTickCount64());
        return s;
    }

    // Returns the genuine advapi32 RegOpenKeyExW, bypassing the alias' __imp_ slot.
    using reg_open_key_ex_w_t = LSTATUS(APIENTRY*)(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY);

    reg_open_key_ex_w_t
    real_reg_open_key_ex_w(HMODULE advapi)
    {
        return reinterpret_cast<reg_open_key_ex_w_t>(
            reinterpret_cast<void*>(::GetProcAddress(advapi, "RegOpenKeyExW")));
    }
}

int
main()
{
    const std::wstring subkey       = unique_subkey();
    const wchar_t      value_name[] = L"AliasProbe";
    const wchar_t      value_data[] = L"hello-windows-win32-shim";
    const DWORD        value_bytes  =
        static_cast<DWORD>((std::wcslen(value_data) + 1) * sizeof(wchar_t));

    std::printf("link-proof: genuine Win32 registry calls redirected into the Rust shim\n");

    // (1) Create a key and write a value through the genuine Win32 API. The alias
    // redirects both into the shim's buffered overlay.
    HKEY    key = nullptr;
    LSTATUS rc  = ::RegCreateKeyExW(HKEY_CURRENT_USER,
                                    subkey.c_str(),
                                    0,
                                    nullptr,
                                    REG_OPTION_NON_VOLATILE,
                                    KEY_READ | KEY_WRITE,
                                    nullptr,
                                    &key,
                                    nullptr);
    report(rc == ERROR_SUCCESS, "RegCreateKeyExW succeeded through the shim");
    report(key != nullptr, "RegCreateKeyExW returned a handle");

    if (rc == ERROR_SUCCESS && key != nullptr)
    {
        rc = ::RegSetValueExW(key,
                              value_name,
                              0,
                              REG_SZ,
                              reinterpret_cast<const BYTE*>(value_data),
                              value_bytes);
        report(rc == ERROR_SUCCESS, "RegSetValueExW succeeded through the shim");

        // Read the value back through the redirected API: confirms the overlay
        // captured it.
        wchar_t readback[64] = {};
        DWORD   readback_cb  = sizeof(readback);
        DWORD   type         = 0;
        rc                   = ::RegQueryValueExW(key,
                                value_name,
                                nullptr,
                                &type,
                                reinterpret_cast<BYTE*>(readback),
                                &readback_cb);
        report(rc == ERROR_SUCCESS, "RegQueryValueExW read the buffered value");
        report(type == static_cast<DWORD>(REG_SZ), "value type is REG_SZ");
        report(std::wcscmp(readback, value_data) == 0, "readback equals what was written");

        report(::RegCloseKey(key) == ERROR_SUCCESS, "RegCloseKey succeeded through the shim");
    }

    // (2) Prove the live registry was never touched: the genuine advapi32
    // RegOpenKeyExW (via GetProcAddress, not redirected) must not find the key.
    HMODULE advapi = ::LoadLibraryW(L"advapi32.dll");
    report(advapi != nullptr, "loaded advapi32.dll for the negative check");

    if (advapi != nullptr)
    {
        const reg_open_key_ex_w_t real_open = real_reg_open_key_ex_w(advapi);
        report(real_open != nullptr, "resolved genuine advapi32 RegOpenKeyExW");

        if (real_open != nullptr)
        {
            HKEY    live_key = nullptr;
            LSTATUS live_rc  =
                real_open(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, &live_key);
            report(live_rc == static_cast<LSTATUS>(ERROR_FILE_NOT_FOUND),
                   "live registry does NOT contain the key (calls were redirected)");

            if (live_rc == ERROR_SUCCESS && live_key != nullptr)
            {
                // Our assumption was wrong and we leaked into the live registry;
                // remove it via the genuine API so the proof does not pollute the
                // machine.
                if (const auto real_close = reinterpret_cast<LSTATUS(APIENTRY*)(HKEY)>(
                        reinterpret_cast<void*>(::GetProcAddress(advapi, "RegCloseKey"))))
                {
                    real_close(live_key);
                }
                if (const auto real_del =
                        reinterpret_cast<LSTATUS(APIENTRY*)(HKEY, LPCWSTR)>(
                            reinterpret_cast<void*>(::GetProcAddress(advapi, "RegDeleteKeyW"))))
                {
                    real_del(HKEY_CURRENT_USER, subkey.c_str());
                }
            }
        }
        ::FreeLibrary(advapi);
    }

    if (g_failures == 0)
    {
        std::printf("link-proof: PASS\n");
        return 0;
    }
    std::printf("link-proof: FAIL (%d check(s) failed)\n", g_failures);
    return 1;
}
