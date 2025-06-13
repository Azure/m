// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <string_view>

#include <Windows.h>

using namespace std::string_view_literals;

struct fmtHRESULT
{
    // HRESULT is defined a (signed!) LONG, but the NTSTATUS
    // constants are #define-d as (DWORD) 0xwhatever. So we have
    // to allow for both signed and unsigned values here.
    constexpr fmtHRESULT(HRESULT hr_in): hr(hr_in) {}
    constexpr fmtHRESULT(int hr_in): hr(hr_in) {}
    constexpr fmtHRESULT(DWORD hr_in): hr(hr_in) {}
    HRESULT hr;
};

template <typename CharT>
struct std::formatter<fmtHRESULT, CharT>
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
    format(fmtHRESULT const& hresult, FormatContext& ctx) const
    {
        auto out = ctx.out();
        auto hr  = hresult.hr;

        //
        // Macros are to be hated but they are to be tolerated here.
        //

#undef X
#define X(p)                                                                                       \
    case p:                                                                                        \
    {                                                                                              \
        constexpr auto lit = #p##sv;                                                               \
        return std::ranges::copy(lit.begin(), lit.end(), out).out;                                 \
    }

        switch (static_cast<DWORD>(hr))
        {
            X(S_OK)
            X(S_FALSE)
            X(E_FAIL)
        }

        if constexpr (std::is_same_v<CharT, char>)
        {
            return std::format_to(out, "0x{:08x}", static_cast<unsigned long>(hr));
        }
        else if constexpr (std::is_same_v<CharT, wchar_t>)
        {
            return std::format_to(out, L"0x{:08x}", static_cast<unsigned long>(hr));
        }
        else
        {
            throw std::runtime_error("Bad CharT");
        }
    }
};
