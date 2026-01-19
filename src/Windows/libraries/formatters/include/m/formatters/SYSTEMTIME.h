// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include <format>

#include <Windows.h>

template <typename CharT>
struct std::formatter<SYSTEMTIME, CharT>
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
    format(SYSTEMTIME const& st, FormatContext& ctx) const
    {
        if constexpr (std::is_same_v<CharT, wchar_t>)
            return std::format_to(ctx.out(),
                                  L"{{ {:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:03d} }}",
                                  st.wYear,
                                  st.wMonth,
                                  st.wDay,
                                  st.wHour,
                                  st.wMinute,
                                  st.wSecond,
                                  st.wMilliseconds);
        else
            return std::format_to(ctx.out(),
                                  "{{ {:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:03d} }}",
                                  st.wYear,
                                  st.wMonth,
                                  st.wDay,
                                  st.wHour,
                                  st.wMinute,
                                  st.wSecond,
                                  st.wMilliseconds);
    }
};
