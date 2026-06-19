// Copyright (c) Microsoft Corporation.
//
// Sample client for the mwin32 link-time alias object.
//
// This is an ORDINARY Win32 registry client: it includes only <windows.h> and
// calls the genuine registry entry points (RegCreateKeyExW, RegSetValueExW,
// RegQueryValueExW, RegEnumValueW, RegDeleteValueW, RegCloseKey). It has no
// knowledge of mwin32 and includes none of its headers. The only thing that makes
// it redirectable is that its CMake target links the `mwin32_alias` object, whose
// __imp_ slots retarget these calls into the mwin32 shim.
//
// The mode (passthrough / logging / buffered / persisted-replay) is chosen entirely
// outside this program by the `<executable>.pilcfg` sidecar the host environment
// places next to it. The same binary therefore drives the whole shim lifecycle:
//   * buffered+capture  — its writes land in an overlay and are persisted, never
//                         touching the live registry;
//   * persisted replay  — it runs against that captured snapshot with no live OS;
//   * logging           — its modifications are recorded for inspection.
//
// It performs a small, representative workload and reports each observation as a
// machine-parseable line on stdout so a harness can assert what the client saw.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace
{
    // The single output site for the whole program. Per the repository's
    // architectural pre-step rule, all reporting is routed through one sink so the
    // formatting/destination concern is separable from the call sites. Lines are
    // emitted as `tag=value` so a harness can parse them without ambiguity.
    class reporter
    {
    public:
        explicit reporter(std::FILE* out) noexcept : m_out(out) {}

        void
        kv(std::string_view tag, std::string_view value) const
        {
            std::fprintf(m_out, "%.*s=%.*s\n",
                         static_cast<int>(tag.size()), tag.data(),
                         static_cast<int>(value.size()), value.data());
        }

        void
        kv(std::string_view tag, unsigned long value) const
        {
            std::fprintf(m_out, "%.*s=%lu\n",
                         static_cast<int>(tag.size()), tag.data(), value);
        }

    private:
        std::FILE* m_out;
    };

    // The registry location and payload the sample reads and writes. The name is
    // fixed (not process-unique) so a capture run and a later replay run agree on
    // exactly which key/values to look for. A single path component is used because
    // the buffered overlay creates one level at a time.
    constexpr wchar_t k_subkey[]       = L"mwin32_sample_client";
    constexpr wchar_t k_value_name[]   = L"name";
    constexpr wchar_t k_value_count[]  = L"count";
    constexpr wchar_t k_value_blob[]   = L"blob";
    constexpr wchar_t k_name_data[]    = L"sample-client";
    constexpr DWORD   k_count_data     = 42u;
    constexpr BYTE    k_blob_data[]    = {0xDEu, 0xADu, 0xBEu, 0xEFu};

    // Convert a narrow ASCII view of a wide string for reporting. The sample's
    // payload is ASCII, so a straight narrowing is sufficient and avoids dragging
    // in locale conversion.
    std::string
    narrow(std::wstring_view w)
    {
        std::string s;
        s.reserve(w.size());
        for (wchar_t c: w)
            s.push_back(c <= 0x7f ? static_cast<char>(c) : '?');
        return s;
    }
}

int
wmain()
{
    const reporter report(stdout);

    // 1) Create (or open) the workload key.
    HKEY    key = nullptr;
    LSTATUS rc  = ::RegCreateKeyExW(HKEY_CURRENT_USER,
                                    k_subkey,
                                    0,
                                    nullptr,
                                    REG_OPTION_NON_VOLATILE,
                                    KEY_READ | KEY_WRITE,
                                    nullptr,
                                    &key,
                                    nullptr);
    report.kv("create_rc", static_cast<unsigned long>(rc));
    if (rc != ERROR_SUCCESS)
        return 1;

    // 2) Write several value types.
    rc = ::RegSetValueExW(key, k_value_name, 0, REG_SZ,
                          reinterpret_cast<const BYTE*>(k_name_data),
                          static_cast<DWORD>((std::wcslen(k_name_data) + 1) * sizeof(wchar_t)));
    report.kv("set_name_rc", static_cast<unsigned long>(rc));

    rc = ::RegSetValueExW(key, k_value_count, 0, REG_DWORD,
                          reinterpret_cast<const BYTE*>(&k_count_data),
                          sizeof(k_count_data));
    report.kv("set_count_rc", static_cast<unsigned long>(rc));

    rc = ::RegSetValueExW(key, k_value_blob, 0, REG_BINARY,
                          k_blob_data, static_cast<DWORD>(sizeof(k_blob_data)));
    report.kv("set_blob_rc", static_cast<unsigned long>(rc));

    // 3) Read the values back and report what the client observed.
    wchar_t name_buf[64] = {};
    DWORD   name_cb      = sizeof(name_buf);
    DWORD   name_type    = 0;
    rc = ::RegQueryValueExW(key, k_value_name, nullptr, &name_type,
                            reinterpret_cast<BYTE*>(name_buf), &name_cb);
    report.kv("get_name_rc", static_cast<unsigned long>(rc));
    if (rc == ERROR_SUCCESS && name_type == REG_SZ)
        report.kv("name", narrow(name_buf));

    DWORD count_data = 0;
    DWORD count_cb   = sizeof(count_data);
    DWORD count_type = 0;
    rc = ::RegQueryValueExW(key, k_value_count, nullptr, &count_type,
                            reinterpret_cast<BYTE*>(&count_data), &count_cb);
    report.kv("get_count_rc", static_cast<unsigned long>(rc));
    if (rc == ERROR_SUCCESS && count_type == REG_DWORD)
        report.kv("count", static_cast<unsigned long>(count_data));

    // 4) Enumerate the value names present on the key. Depending on the active PIL
    // stack the shim may not implement value enumeration yet (it can answer
    // ERROR_NOT_SUPPORTED); the client handles that gracefully rather than treating
    // it as data loss.
    unsigned long value_count   = 0;
    bool          enum_supported = true;
    for (DWORD i = 0;; ++i)
    {
        wchar_t ename[64] = {};
        DWORD   ename_cb  = static_cast<DWORD>(std::size(ename));
        LSTATUS erc = ::RegEnumValueW(key, i, ename, &ename_cb,
                                      nullptr, nullptr, nullptr, nullptr);
        if (erc == ERROR_NO_MORE_ITEMS)
            break;
        if (erc != ERROR_SUCCESS)
        {
            report.kv("enum_rc", static_cast<unsigned long>(erc));
            enum_supported = false;
            break;
        }
        ++value_count;
    }
    if (enum_supported)
        report.kv("value_count", value_count);

    // 5) Delete one value and confirm it is gone.
    rc = ::RegDeleteValueW(key, k_value_blob);
    report.kv("delete_blob_rc", static_cast<unsigned long>(rc));

    DWORD probe_type = 0;
    rc = ::RegQueryValueExW(key, k_value_blob, nullptr, &probe_type, nullptr, nullptr);
    report.kv("blob_after_delete_rc", static_cast<unsigned long>(rc));

    ::RegCloseKey(key);
    return 0;
}
