// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include <m/strings/compare.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(LinuxStringCompare, Basic1)
{
    auto x = m::case_insensitive_less<std::wstring>{};

    EXPECT_TRUE(x(L"a"s, L"b"s));
}

TEST(LinuxStringCompare, Basic2)
{
    auto x = m::case_insensitive_less<std::wstring>{};

    EXPECT_TRUE(x(L"a"s, L"B"s));
}
TEST(LinuxStringCompare, Basic3)
{
    auto x = m::case_insensitive_less<std::wstring>{};

    EXPECT_TRUE(x(L"a"s, L"banana"s));
}
TEST(LinuxStringCompare, Basic4)
{
    auto x = m::case_insensitive_less<std::wstring>{};

    EXPECT_TRUE(x(L"apple"s, L"b"s));
}
TEST(LinuxStringCompare, Basic5)
{
    auto x = m::case_insensitive_less<std::wstring>{};

    EXPECT_TRUE(x(L"apple"s, L"banana"s));
}

TEST(LinuxStringCompare, Basic6)
{
    auto x = m::case_insensitive_less<std::wstring>{};

    EXPECT_TRUE(x(L"apple"s, L"BANANA"s));
}

TEST(LinuxStringCompare, NonReflexive1)
{
    auto const x = m::case_insensitive_less<std::wstring>{};
    EXPECT_FALSE(x(L"a"s, L"a"s));
}

TEST(LinuxStringCompare, NonReflexive2)
{
    auto const x = m::case_insensitive_less<std::wstring>{};
    EXPECT_FALSE(x(L"A"s, L"a"s));
}

TEST(LinuxStringCompare, NonReflexive3)
{
    auto const x = m::case_insensitive_less<std::wstring>{};
    EXPECT_FALSE(x(L"a"s, L"A"s));
}

TEST(LinuxStringCompare, NonReflexive4)
{
    auto const x = m::case_insensitive_less<std::wstring>{};
    EXPECT_FALSE(x(L"A"s, L"A"s));
}

TEST(LinuxStringCompare, Views1)
{
    auto const x = m::case_insensitive_less<std::wstring>{};
    EXPECT_TRUE(x(L"apple"s, L"BANANA"s));
}

TEST(LinuxStringCompare, Views2)
{
    auto const x = m::case_insensitive_less<std::wstring>{};
    EXPECT_TRUE(x(L"apple"s, L"BANANA"sv));
}
TEST(LinuxStringCompare, Views3)
{
    auto const x = m::case_insensitive_less<std::wstring>{};
    EXPECT_TRUE(x(L"apple"sv, L"BANANA"s));
}
TEST(LinuxStringCompare, Views4)
{
    auto const x = m::case_insensitive_less<std::wstring>{};
    EXPECT_TRUE(x(L"apple"sv, L"BANANA"sv));
}
