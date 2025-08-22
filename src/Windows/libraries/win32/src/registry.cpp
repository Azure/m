// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <map>
#include <tuple>

#include <m/error_handling/macros.h>
#include <m/errors/errors.h>
#include <m/strings/compare.h>
#include <m/win32/registry.h>

using namespace std::string_literals;

namespace m::win32::registry
{
    namespace registry_impl
    {
        using P = std::pair<m::wsstring, predefined_key>;

        inline const std::map<m::wsstring, predefined_key, m::case_insensitive_less<m::wsstring>>
            predefined_key_names = {{
                P{L"HKCR", predefined_key::classes_root},
                P{L"HKEY_CLASSES_ROOT", predefined_key::classes_root},
                P{L"HKCU", predefined_key::current_user},
                P{L"HKEY_CURRENT_USER", predefined_key::current_user},
                P{L"HKLM", predefined_key::local_machine},
                P{L"HKEY_LOCAL_MACHINE", predefined_key::local_machine},
                P{L"HKEY_USERS", predefined_key::users},
                P{L"HKEY_PERFORMANCE_DATA", predefined_key::performance_data},
                P{L"HKEY_CURRENT_CONFIG", predefined_key::current_config},
                P{L"HKCC", predefined_key::current_config},
                P{L"HKEY_CURRENT_USER_LOCAL_SETTINGS", predefined_key::current_user_local_settings},
                P{L"HKEY_PERFORMANCE_TEXT", predefined_key::performance_text},
                P{L"HKEY_PERFORMANCE_NLSTEXT", predefined_key::performance_nlstext},
            }};

    } // namespace registry_impl

    std::optional<predefined_key>
    try_map_string_to_predefined_key(m::wsstring str)
    {
        auto it = registry_impl::predefined_key_names.find(str);

        if (it != registry_impl::predefined_key_names.end())
            return it->second;

        return std::nullopt;
    }

    void
    hkey::open(predefined_key pk, PCWSTR sub_key, REGSAM sam_desired)
    {
        hkey       hk;
        auto const status =
            ::RegOpenKeyExW(reinterpret_cast<HKEY>(pk), sub_key, 0, sam_desired, hk.addressof());
        if (status != ERROR_SUCCESS)
        {
            m::throw_win32_error_code(status);
        }

        hk.swap(*this);
    }

    /// <summary>
    /// Open? -> openq meaning open as a question, returns an error_code indicating
    /// whether the open succeeded or not. If not, the hkey is left
    /// unchanged.
    /// </summary>
    /// <param name="pk"></param>
    /// <param name="sub_key"></param>
    /// <param name="sam_desired"></param>
    /// <returns></returns>
    [[nodiscard]] std::error_code
    hkey::openq(predefined_key pk, PCWSTR sub_key, REGSAM sam_desired)
    {
        hkey       hk;
        auto const status =
            ::RegOpenKeyExW(reinterpret_cast<HKEY>(pk), sub_key, 0, sam_desired, hk.addressof());
        if (status != ERROR_SUCCESS)
        {
            return make_win32_error_code(status);
        }

        hk.swap(*this);
        return std::error_code{};
    }

    void
    hkey::open(hkey const& hkey_base, PCWSTR sub_key, REGSAM sam_desired)
    {
        hkey       hk;
        auto const status =
            ::RegOpenKeyExW(hkey_base.get(), sub_key, 0, sam_desired, hk.addressof());
        if (status != ERROR_SUCCESS)
        {
            m::throw_win32_error_code(status);
        }

        hk.swap(*this);
    }

    [[nodiscard]] std::error_code
    hkey::openq(hkey const& hkey_base, PCWSTR sub_key, REGSAM sam_desired)
    {
        hkey       hk;
        auto const status =
            ::RegOpenKeyExW(hkey_base.get(), sub_key, 0, sam_desired, hk.addressof());
        if (status != ERROR_SUCCESS)
        {
            return make_win32_error_code(status);
        }

        hk.swap(*this);
        return std::error_code{};
    }

    void
    hkey::notify_change_value(bool           watch_subtree,
                              notify_filters filters,
                              HANDLE         evt,
                              bool           asynchronous)
    {
        M_INTERNAL_ERROR_CHECK(is_valid());
        auto const status = ::RegNotifyChangeKeyValue(
            m_hkey, watch_subtree, static_cast<DWORD>(filters), evt, asynchronous);
        if (status != ERROR_SUCCESS)
        {
            m::throw_win32_error_code(status);
        }
    }

    [[nodiscard]] std::error_code
    hkey::notify_change_valueq(bool           watch_subtree,
                               notify_filters filters,
                               HANDLE         evt,
                               bool           asynchronous)
    {
        M_INTERNAL_ERROR_CHECK(is_valid());
        auto const status = ::RegNotifyChangeKeyValue(
            m_hkey, watch_subtree, static_cast<DWORD>(filters), evt, asynchronous);
        if (status != ERROR_SUCCESS)
        {
            return make_win32_error_code(status);
        }

        return std::error_code{};
    }

} // namespace m::win32::registry