// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <string_view>

#include <Windows.h>

typedef _Return_type_success_(return >= 0) LONG NTSTATUS;

using namespace std::string_view_literals;

struct fmtNTSTATUS
{
    // NTSTATUS is defined a (signed!) LONG, but the NTSTATUS
    // constants are #define-d as (DWORD) 0xwhatever. So we have
    // to allow for both signed and unsigned values here.
    constexpr fmtNTSTATUS(int s_in): s(s_in) {}
    constexpr fmtNTSTATUS(LONG s_in): s(s_in) {}
    constexpr fmtNTSTATUS(DWORD s_in): s(s_in) {}
    NTSTATUS s;
};

template <typename CharT>
struct std::formatter<fmtNTSTATUS, CharT>
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
    format(fmtNTSTATUS const& ntstatus, FormatContext& ctx) const
    {
        auto out = ctx.out();
        auto s   = ntstatus.s;

        switch (static_cast<DWORD>(s))
        {
            case STATUS_PENDING: return std::ranges::copy(L"STATUS_PENDING"sv, out).out;
            case STATUS_WAIT_0: return std::ranges::copy(L"STATUS_WAIT_0"sv, out).out;
            case STATUS_ABANDONED_WAIT_0:
                return std::ranges::copy(L"STATUS_ABANDONED_WAIT_0"sv, out).out;
            case STATUS_INVALID_HANDLE:
                return std::ranges::copy(L"STATUS_INVALID_HANDLE"sv, out).out;
            case STATUS_INVALID_PARAMETER:
                return std::ranges::copy(L"STATUS_INVALID_PARAMETER"sv, out).out;
        }

        if constexpr (std::is_same_v<CharT, char>)
        {
            return std::format_to(out, "0x{:08x}", static_cast<unsigned long>(s));
        }
        else if constexpr (std::is_same_v<CharT, wchar_t>)
        {
            return std::format_to(out, L"0x{:08x}", static_cast<unsigned long>(s));
        }
        else
        {
            throw std::runtime_error("Bad CharT");
        }
    }
};
