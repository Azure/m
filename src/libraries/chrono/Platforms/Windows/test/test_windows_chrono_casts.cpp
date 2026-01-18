// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <chrono>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <m/formatters/FILETIME.h>
#include <m/formatters/SYSTEMTIME.h>
#include <m/print/print.h>
#include <m/windows_chrono/windows_chrono_casts.h>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

using utc_clock_type  = std::chrono::utc_clock;
using time_point_type = utc_clock_type::time_point;

TEST(WindowsChronoCasts, SimpleTest1)
{
    auto const utctime1 = time_point_type(); // Notes
    // The official UTC epoch is 1 January 1972. utc_clock uses 1 January 1970 instead to be
    // consistent with std::chrono::system_clock.
    //

    auto const systime = m::utc_time_point_cast<SYSTEMTIME>(utctime1);

    EXPECT_EQ(systime.wYear, 1970);
    EXPECT_EQ(systime.wMonth, 1);
    EXPECT_EQ(systime.wDayOfWeek, 4);
    EXPECT_EQ(systime.wDay, 1);
    EXPECT_EQ(systime.wHour, 0);
    EXPECT_EQ(systime.wMinute, 0);
    EXPECT_EQ(systime.wSecond, 0);
    EXPECT_EQ(systime.wMilliseconds, 0);
}

TEST(WindowsChronoCasts, SimpleTest2)
{
    time_point_type utctime;

    auto time_string = "7/22/1998 3:15:42"s;
    auto stream      = std::istringstream(time_string);

    std::chrono::from_stream(stream, "%m/%d/%Y %H:%M:%S", utctime);

    // The official UTC epoch is 1 January 1972. utc_clock uses 1 January 1970 instead to be
    // consistent with std::chrono::system_clock.
    //

    auto const systime = m::utc_time_point_cast<SYSTEMTIME>(utctime);

    EXPECT_EQ(systime.wYear, 1998);
    EXPECT_EQ(systime.wMonth, 7);
    EXPECT_EQ(systime.wDayOfWeek, 3);
    EXPECT_EQ(systime.wDay, 22);
    EXPECT_EQ(systime.wHour, 3);
    EXPECT_EQ(systime.wMinute, 15);
    EXPECT_EQ(systime.wSecond, 42);
    EXPECT_EQ(systime.wMilliseconds, 0);
}

TEST(WindowsChronoCasts, DwordAsMsTestWithTo)
{
    auto const d = 100ms;
    auto       x = m::to<m::win32_dword_ms>(d);
    EXPECT_EQ(static_cast<DWORD>(x), 100);
}

TEST(WindowsChronoCasts, FILETIMECasting)
{
    auto const d = 100ms;
    auto       x = m::to<FILETIME>(d);

    m::println("Result conversion {} -> {}", d, fmtFILETIME{x});
}

TEST(WindowsChronoCasts, CastFILETIMEToUtcTime)
{
    SYSTEMTIME st{};

    st.wYear         = 2000;
    st.wMonth        = 1;
    st.wDay          = 1;
    st.wHour         = 0;
    st.wMinute       = 0;
    st.wSecond       = 0;
    st.wMilliseconds = 0;

    FILETIME ft{};

    EXPECT_NE(::SystemTimeToFileTime(&st, &ft), 0);

    m::println("Result conversion {} -> {}", st, fmtFILETIME{ft});

    auto utc_tp = m::clock_cast<m::utc_clock_type>(ft);
    m::println("Converted to utc_time_point: {}", utc_tp);
}
