// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <string_view>

#include <Windows.h>

using namespace std::string_view_literals;

struct fmtWin32ErrorCode
{
    DWORD dwErrorCode;
};

template <typename CharT>
struct std::formatter<fmtWin32ErrorCode, CharT>
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
    format(fmtWin32ErrorCode const& w32ec, FormatContext& ctx) const
    {
        auto       out = ctx.out();
        auto const ec  = w32ec.dwErrorCode;

        wstring_view sv{};
        switch (ec)
        {
            case ERROR_INVALID_FUNCTION: sv = L"ERROR_INVALID_FUNCTION"sv; break;
            case ERROR_FILE_NOT_FOUND: sv = L"ERROR_FILE_NOT_FOUND"sv; break;
            case ERROR_PATH_NOT_FOUND: sv = L"ERROR_PATH_NOT_FOUND"sv; break;
            case ERROR_ACCESS_DENIED: sv = L"ERROR_ACCESS_DENIED"sv; break;
            case ERROR_INVALID_HANDLE: sv = L"ERROR_INVALID_HANDLE"sv; break;
            case ERROR_SHARING_VIOLATION: sv = L"ERROR_SHARING_VIOLATION"sv; break;
        }

        if (sv.size() != 0)
            return std::format_to(out, L"{{ {} ({}) }}", sv, ec);

        return std::format_to(out, L"{{ {} }}", ec);
    }
};
