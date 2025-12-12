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
        using namespace std::string_view_literals;

        using P = std::pair<m::wsstring, predefined_key>;

        inline const std::map<m::wsstring, predefined_key, m::case_insensitive_less<m::wsstring>>
            predefined_key_names = {{
                P{L"HKCR"sv, predefined_key::classes_root},
                P{L"HKEY_CLASSES_ROOT"sv, predefined_key::classes_root},
                P{L"HKCU"sv, predefined_key::current_user},
                P{L"HKEY_CURRENT_USER"sv, predefined_key::current_user},
                P{L"HKLM"sv, predefined_key::local_machine},
                P{L"HKEY_LOCAL_MACHINE"sv, predefined_key::local_machine},
                P{L"HKEY_USERS"sv, predefined_key::users},
                P{L"HKEY_PERFORMANCE_DATA"sv, predefined_key::performance_data},
                P{L"HKEY_CURRENT_CONFIG"sv, predefined_key::current_config},
                P{L"HKCC"sv, predefined_key::current_config},
                P{L"HKEY_CURRENT_USER_LOCAL_SETTINGS"sv,
                  predefined_key::current_user_local_settings},
                P{L"HKEY_PERFORMANCE_TEXT"sv, predefined_key::performance_text},
                P{L"HKEY_PERFORMANCE_NLSTEXT"sv, predefined_key::performance_nlstext},
            }};

        struct predefined_hkey_data
        {
            predefined_key m_predefined_key;
        };

        predefined_hkey_data const*
        try_find_predefined_hkey_data(HKEY hkey)
        {
#pragma push_macro("QQ")
#undef QQ
#define QQ(p1, p2)                                                                                 \
    case reinterpret_cast<uintptr_t>(p1):                                                          \
    {                                                                                              \
        static const predefined_hkey_data static_##p2##_data{.m_predefined_key =                   \
                                                                 predefined_key::p2};              \
        return &static_##p2##_data;                                                                \
    }

            switch (reinterpret_cast<uintptr_t>(hkey))
            {
                QQ(HKEY_CLASSES_ROOT, classes_root)
                QQ(HKEY_CURRENT_USER, current_user)
                QQ(HKEY_LOCAL_MACHINE, local_machine)
                QQ(HKEY_USERS, users)
                QQ(HKEY_PERFORMANCE_DATA, performance_data)
                QQ(HKEY_CURRENT_CONFIG, current_config)
                QQ(HKEY_CURRENT_USER_LOCAL_SETTINGS, current_user_local_settings)
                QQ(HKEY_PERFORMANCE_TEXT, performance_text)
                QQ(HKEY_PERFORMANCE_NLSTEXT, performance_nlstext)
            }
#pragma pop_macro("QQ")

            return nullptr;
        }

    } // namespace registry_impl

    std::optional<predefined_key>
    try_map_string_to_predefined_key(m::wsstring str)
    {
        auto it = registry_impl::predefined_key_names.find(str);

        if (it != registry_impl::predefined_key_names.end())
            return it->second;

        return std::nullopt;
    }

    std::optional<predefined_key>
    try_map_hkey_to_predefined_key(HKEY hkey) noexcept
    {
        auto pdata = registry_impl::try_find_predefined_hkey_data(hkey);

        if (pdata == nullptr)
            return std::nullopt;

        return pdata->m_predefined_key;
    }

    bool
    is_predefined_hkey(HKEY hkey) noexcept
    {
        return registry_impl::try_find_predefined_hkey_data(hkey) != nullptr;
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