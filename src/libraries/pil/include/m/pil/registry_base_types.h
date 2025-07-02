// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <chrono>
#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <m/strings/convert.h>
#include <m/utility/utility.h>

#ifdef WIN32
#include <m/windows_strings/convert.h>
#else
#include <m/linux_strings/convert.h>
#endif

#include "common.h"
#include "disposition.h"
#include "security_attributes.h"

namespace m::pil::registry
{
    //
    //  The registry_char, registry_string, and registry_string_view types
    // are the types of the strings that are presented to the client of the
    // "friendly" registry API provided here.
    //
    // It is not the format of the strings that are stored in the physical
    // registry values which is uniformly UTF-16, since while we are making
    // a hopefully easy to use API here with the ability to have various
    // features for testability and other utility, at its heart, the registry
    // is a hierarchical database.
    //

#ifdef WIN32

    // On Windows we want to convert to the Windows UTF-16 types that
    // clients expect
    using registry_char        = wchar_t;
    using registry_string      = std::wstring;
    using registry_string_view = std::wstring_view;

    template <typename T>
    registry_string
    to_registry_string(T const& s)
    {
        return m::to_wstring(s);
    }

#else

    // On Linux, convert to the de facto UTF-8 types that people expect

    using registry_char        = char;
    using registry_string      = std::string;
    using registry_string_view = std::string_view;

    template <typename T>
    registry_string
    to_registry_string(T const& s)
    {
        return m::to_string(s);
    }

#endif

    //
    // These types generally are not that interesting to clients unless you use the
    // "raw" std::byte span/vector returning APIs.
    //
    using registry_storage_char        = char16_t;
    using registry_storage_string      = std::u16string;
    using registry_storage_string_view = std::u16string_view;

    template <typename T>
    registry_storage_string
    to_registry_storage_string(T const& v)
    {
        return m::to_u16string(v);
    }

    template <typename T>
    void
    to_registry_storage_string(T const& v, registry_storage_string& str)
    {
        m::to_u16string(v, str);
    }

    template <typename CharT>
    registry_storage_string
    to_null_terminated_registry_storage_string(std::basic_string_view<CharT> const& view)
    {
        registry_storage_string value;
        to_null_terminated_registry_storage_string(view, value);
        return value;
    }

    template <typename CharT>
    void
    to_null_terminated_registry_storage_string(std::basic_string_view<CharT> const& view, registry_storage_string& value)
    {
        using namespace std::string_view_literals;

        to_registry_storage_string(view, value);

        if ((value.size() == 0) || (value[value.size() - 1] != u'\0'))
            value.append(u"\0"sv);
    }

    registry_storage_string
    to_double_null_terminated_registry_storage_string(std::vector<registry_string> const& v);

    enum class predefined_key : uint32_t
    {
        classes_root                = 0x80000000ul, // HKEY_CLASSES_ROOT
        current_user                = 0x80000001ul, // HKEY_CURRENT_USER
        local_machine               = 0x80000002ul, // HKEY_LOCAL_MACHINE
        users                       = 0x80000003ul, // HKEY_USERS
        performance_data            = 0x80000004ul, // HKEY_PERFORMANCE_DATA
        current_config              = 0x80000005ul, // HKEY_CURRENT_CONFIG
        current_user_local_settings = 0x80000007ul, // HKEY_CURRENT_USER_LOCAL_SETTINGS
        performance_text            = 0x80000050ul, // HKEY_PERFORMANCE_TEXT
        performance_nlstext         = 0x80000060ul, // HKEY_PERFORMANCE_NLSTEXT
    };

    enum class value_type : uint32_t
    {
        none                 = 0, // REG_NONE
        string               = 1, // REG_SZ
        expand_string        = 2, // REG_EXPAND_SZ
        binary               = 3, // REG_BINARY
        uint32               = 4, // REG_DWORD
        uint32_little_endian = 4, // REG_DWORD_LITTLE_ENDIAN
        uint32_big_endian    = 5, // REG_DWORD_BIG_ENDIAN
        link                 = 6, // REG_LINK
        multi_string         = 7, // REG_MULTI_SZ
        // 8, 9, and 10 are descriptions of data regarding hardware and are
        // not documented
        uint64               = 11, // REG_QWORD
        uint64_little_endian = 11, // REG_QWORD_LITTLE_ENDIAN
        // there never was a REG_QWORD_BIG_ENDIAN
    };

    enum class sam : uint32_t
    {
        maximum_allowed    = 0x02000000ul,
        default_create_key = maximum_allowed, // default access mask for create_key
        default_delete_key = maximum_allowed, // default access mask for delete_key
        default_open_key   = maximum_allowed, // default access mask for open_key
    };
} // namespace m::pil::registry
