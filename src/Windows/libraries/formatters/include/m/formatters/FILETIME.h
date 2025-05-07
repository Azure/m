// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include <array>
#include <chrono>
#include <format>
#include <string_view>

#include <Windows.h>

struct fmtFILETIME
{
    FILETIME filetime;
};

inline FILETIME
toFILETIME(LARGE_INTEGER li)
{
    union
    {
        LARGE_INTEGER m_li;
        FILETIME      m_ft;
    } u{li};
    return u.m_ft;
}

template <typename CharT>
struct std::formatter<fmtFILETIME, CharT>
{
    union LocalUnion
    {
        FILETIME filetime;
        int64_t  rep;
    };

    static_assert(sizeof(LocalUnion) == sizeof(int64_t));
    static_assert(sizeof(LocalUnion) == sizeof(FILETIME));

    //
    // FILETIME is 100ns units, so for the ratio
    // have the denominator be 1 billion divided by
    // 100.
    //
    using FiletimeRatio    = std::ratio<1, 1'000'000'000 / 100>;
    using FiletimeDuration = std::chrono::duration<int64_t, FiletimeRatio>;

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
    format(fmtFILETIME const& filetimeIn, FormatContext& ctx) const
    {
        auto out = ctx.out();

        FILETIME ft = filetimeIn.filetime;

        // Negative FILETIMEs are durations; positive FILETIMEs are
        // date/times

        if (ft.dwHighDateTime & 0x80000000)
        {
            ULARGE_INTEGER uli;
            uli.HighPart = ft.dwHighDateTime;
            uli.LowPart  = ft.dwLowDateTime;

            auto const microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
                FiletimeDuration(uli.QuadPart));
            double dus = -static_cast<double>(microseconds.count());
            double dms = dus / 1000.0;
            if constexpr (std::is_same_v<CharT, wchar_t>)
                out = std::format_to(out, L"{{ms: {}}}", dms);
            else
                out = std::format_to(out, "{{ms: {}}}", dms);
        }
        else
        {
            constexpr std::array<wchar_t const*, 8> wdays{
                L"Su", L"Mo", L"Tu", L"We", L"Th", L"Fr", L"Sa", L"OutOfRange"};
            constexpr std::array<char const*, 8> days{
                "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa", "OutOfRange"};
            SYSTEMTIME st{};
            ::FileTimeToSystemTime(&ft, &st);
            if constexpr (std::is_same_v<CharT, wchar_t>)
                out = std::format_to(
                    out,
                    L"{{ {} {:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:03d} }}",
                    wdays[(std::min)(static_cast<size_t>(7), static_cast<size_t>(st.wDayOfWeek))],
                    st.wYear,
                    st.wMonth,
                    st.wDay,
                    st.wHour,
                    st.wMinute,
                    st.wHour,
                    st.wMinute,
                    st.wSecond,
                    st.wMilliseconds);
            else
                out = std::format_to(
                    out,
                    "{{ {} {:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:03d} }}",
                    days[(std::min)(static_cast<size_t>(7), static_cast<size_t>(st.wDayOfWeek))],
                    st.wYear,
                    st.wMonth,
                    st.wDay,
                    st.wHour,
                    st.wMinute,
                    st.wHour,
                    st.wMinute,
                    st.wSecond,
                    st.wMilliseconds);
        }

        return out;
    }
};
