// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <string_view>

#include <Windows.h>

using namespace std::string_view_literals;

template <typename CharT>
struct std::formatter<HKEY, CharT>
{
    template <typename ParseContext>
    constexpr ParseContext::iterator
    parse(ParseContext& ctx)
    {
        auto       it  = ctx.begin();
        auto const end = ctx.end();

        if (it != end && *it != '}')
            throw std::format_error("Invalid format string");

        return it;
    }

    template <typename FormatContext>
    FormatContext::iterator
    format(HKEY hkey, FormatContext& ctx) const
    {
        auto out = ctx.out();

        //
        // Macros are to be hated but they are to be tolerated here.
        //
        // There are a number of "named" HKEY values.
        //

#pragma push_macro("X")

#undef X
#define X(p)                                                                                       \
    if (hkey == p)                                                                                 \
    {                                                                                              \
        constexpr auto lit = #p##sv;                                                               \
        return std::ranges::copy(lit.begin(), lit.end(), out).out;                                 \
    }

        X(HKEY_CURRENT_USER)
        X(HKEY_CLASSES_ROOT)
        X(HKEY_LOCAL_MACHINE)
        X(HKEY_USERS)
        X(HKEY_PERFORMANCE_DATA)
        X(HKEY_PERFORMANCE_TEXT)
        X(HKEY_PERFORMANCE_NLSTEXT)
        X(HKEY_CURRENT_CONFIG)
        X(HKEY_DYN_DATA)
        X(HKEY_CURRENT_USER_LOCAL_SETTINGS)

#pragma pop_macro("X")

        if constexpr (std::is_same_v<CharT, char>)
        {
            return std::format_to(out, "{{hkey 0x{:x}}}", reinterpret_cast<uintptr_t>(hkey));
        }
        else if constexpr (std::is_same_v<CharT, wchar_t>)
        {
            return std::format_to(out, L"{{hkey 0x{:x}}}", reinterpret_cast<uintptr_t>(hkey));
        }
        else
        {
            throw std::runtime_error("Bad CharT");
        }
    }
};
