// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <string>
#include <string_view>

#include <m/formatters/FILETIME.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

//
// These are just two FILETIME values chosen more or less at random. The positive one was
// just adjusted to be somewhat near to the current year of 2025.
//
constexpr FILETIME ft1{0x0, 0x01e00000};
constexpr FILETIME negative_ft{0x0, 0x81e00000};

TEST(FILETIME, lbasic)
{
    auto s = std::format(L"{}", fmtFILETIME(ft1));
    EXPECT_EQ(s, L"{ Tu 2029-02-20 23:41:23.041 }"s);
}

TEST(FILETIME, lnegative)
{
    auto s = std::format(L"{}", fmtFILETIME(negative_ft));
    EXPECT_EQ(s, L"{ms: 908826404803366.1}"s);
}

TEST(FILETIME, basic)
{
    auto s = std::format("{}", fmtFILETIME(ft1));
    EXPECT_EQ(s, "{ Tu 2029-02-20 23:41:23.041 }"s);
}

TEST(FILETIME, negative)
{
    auto s = std::format("{}", fmtFILETIME(negative_ft));
    EXPECT_EQ(s, "{ms: 908826404803366.1}"s);
}




