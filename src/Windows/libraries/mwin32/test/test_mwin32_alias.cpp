// Copyright (c) Microsoft Corporation.
//
// M-ALIAS-4: link-proof integration test for the mwin32 alias object.
//
// This translation unit deliberately includes NO mwin32 headers. It calls the
// genuine Win32 registry entry points (RegCreateKeyExW / RegSetValueExW /
// RegQueryValueExW / RegCloseKey). The executable links the `mwin32_alias` object,
// whose __imp_ slot definitions redirect those calls into the mwin32 shim. With a
// buffered `.pilcfg` next to the executable, every write must land in the shim's
// in-memory overlay and never reach the live registry.
//
// Redirection is proven two ways:
//   (1) the value written through the redirected Win32 API reads back through the
//       redirected Win32 API (the buffered overlay captured it), and
//   (2) the REAL advapi32 entry points, obtained via GetProcAddress (which the
//       alias deliberately does not redirect), cannot find the write in the live
//       registry.

#include <windows.h>

#include <gtest/gtest.h>

#include <cstring>
#include <string>

namespace
{
    // A subkey name unique to this process+run so concurrent or repeated runs do
    // not collide, and so a stray live-registry entry (which would indicate a
    // redirection failure) is unambiguous. A single path component is used because
    // the buffered overlay's create_key operates one level at a time (it does not
    // auto-create intermediate keys the way live RegCreateKeyExW does).
    std::wstring
    unique_subkey()
    {
        std::wstring s = L"mwin32_alias_test_";
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

TEST(Mwin32AliasRedirect, GenuineWin32CallsReachShimNotLiveRegistry)
{
    const std::wstring subkey      = unique_subkey();
    const wchar_t      value_name[] = L"AliasProbe";
    const wchar_t      value_data[] = L"hello-mwin32";
    const DWORD        value_bytes  =
        static_cast<DWORD>((std::wcslen(value_data) + 1) * sizeof(wchar_t));

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
    ASSERT_EQ(rc, ERROR_SUCCESS) << "RegCreateKeyExW did not succeed through the shim";
    ASSERT_NE(key, nullptr);

    rc = ::RegSetValueExW(key,
                          value_name,
                          0,
                          REG_SZ,
                          reinterpret_cast<const BYTE*>(value_data),
                          value_bytes);
    ASSERT_EQ(rc, ERROR_SUCCESS) << "RegSetValueExW did not succeed through the shim";

    // Read the value back through the redirected API: confirms the overlay captured it.
    wchar_t readback[64] = {};
    DWORD   readback_cb  = sizeof(readback);
    DWORD   type         = 0;
    rc                   = ::RegQueryValueExW(key,
                            value_name,
                            nullptr,
                            &type,
                            reinterpret_cast<BYTE*>(readback),
                            &readback_cb);
    EXPECT_EQ(rc, ERROR_SUCCESS) << "RegQueryValueExW did not read the buffered value";
    EXPECT_EQ(type, static_cast<DWORD>(REG_SZ));
    EXPECT_STREQ(readback, value_data);

    EXPECT_EQ(::RegCloseKey(key), ERROR_SUCCESS);

    // (2) Prove the live registry was never touched: the genuine advapi32
    // RegOpenKeyExW (via GetProcAddress, not redirected) must not find the key.
    HMODULE advapi = ::LoadLibraryW(L"advapi32.dll");
    ASSERT_NE(advapi, nullptr);
    const reg_open_key_ex_w_t real_open = real_reg_open_key_ex_w(advapi);
    ASSERT_NE(real_open, nullptr);

    HKEY    live_key = nullptr;
    LSTATUS live_rc  =
        real_open(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, &live_key);
    EXPECT_EQ(live_rc, static_cast<LSTATUS>(ERROR_FILE_NOT_FOUND))
        << "the live registry contains the key; calls were not redirected to the shim";
    if (live_rc == ERROR_SUCCESS && live_key != nullptr)
    {
        // Our assumption was wrong and we leaked into the live registry; remove it
        // via the genuine API so the test does not pollute the machine.
        if (const auto real_close = reinterpret_cast<LSTATUS(APIENTRY*)(HKEY)>(
                reinterpret_cast<void*>(::GetProcAddress(advapi, "RegCloseKey"))))
        {
            real_close(live_key);
        }
        if (const auto real_del = reinterpret_cast<LSTATUS(APIENTRY*)(HKEY, LPCWSTR)>(
                reinterpret_cast<void*>(::GetProcAddress(advapi, "RegDeleteKeyW"))))
        {
            real_del(HKEY_CURRENT_USER, subkey.c_str());
        }
    }
    ::FreeLibrary(advapi);
}
