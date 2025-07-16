// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include <m/strings/compare.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(WindowsStrings_Compare_wstring_view, Basic1)
{
    auto x = m::case_insensitive_less<std::wstring_view>{};

    EXPECT_TRUE(x(L"a"s, L"b"s));
}

TEST(WindowsStrings_Compare_wstring_view, Basic2)
{
    auto x = m::case_insensitive_less<std::wstring_view>{};

    EXPECT_TRUE(x(L"a"s, L"B"s));
}
TEST(WindowsStrings_Compare_wstring_view, Basic3)
{
    auto x = m::case_insensitive_less<std::wstring_view>{};

    EXPECT_TRUE(x(L"a"s, L"banana"s));
}
TEST(WindowsStrings_Compare_wstring_view, Basic4)
{
    auto x = m::case_insensitive_less<std::wstring_view>{};

    EXPECT_TRUE(x(L"apple"s, L"b"s));
}
TEST(WindowsStrings_Compare_wstring_view, Basic5)
{
    auto x = m::case_insensitive_less<std::wstring_view>{};

    EXPECT_TRUE(x(L"apple"s, L"banana"s));
}

TEST(WindowsStrings_Compare_wstring_view, Basic6)
{
    auto x = m::case_insensitive_less<std::wstring_view>{};

    EXPECT_TRUE(x(L"apple"s, L"BANANA"s));
}

TEST(WindowsStrings_Compare_wstring_view, NonReflexive1)
{
    auto const x = m::case_insensitive_less<std::wstring_view>{};
    EXPECT_FALSE(x(L"a"s, L"a"s));
}

TEST(WindowsStrings_Compare_wstring_view, NonReflexive2)
{
    auto const x = m::case_insensitive_less<std::wstring_view>{};
    EXPECT_FALSE(x(L"A"s, L"a"s));
}

TEST(WindowsStrings_Compare_wstring_view, NonReflexive3)
{
    auto const x = m::case_insensitive_less<std::wstring_view>{};
    EXPECT_FALSE(x(L"a"s, L"A"s));
}

TEST(WindowsStrings_Compare_wstring_view, NonReflexive4)
{
    auto const x = m::case_insensitive_less<std::wstring_view>{};
    EXPECT_FALSE(x(L"A"s, L"A"s));
}

TEST(WindowsStrings_Compare_wstring_view, Gt_B_A)
{
    auto const x = m::case_insensitive_less<std::wstring_view>();
    EXPECT_FALSE(x(L"B"s, L"A"s));
}

TEST(WindowsStrings_Compare_wstring_view, Gt_B_a)
{
    auto const x = m::case_insensitive_less<std::wstring_view>();
    EXPECT_FALSE(x(L"B"s, L"a"s));
}

TEST(WindowsStrings_Compare_wstring_view, Gt_Banana_Apple)
{
    auto const x = m::case_insensitive_less<std::wstring_view>();
    EXPECT_FALSE(x(L"Banana"s, L"Apple"s));
}

TEST(WindowsStrings_Compare_wstring_view, Gt_BANANA_apple)
{
    auto const x = m::case_insensitive_less<std::wstring_view>();
    EXPECT_FALSE(x(L"BANANA"s, L"apple"s));
}

TEST(WindowsStrings_Compare_wstring_view, Views1)
{
    auto const x = m::case_insensitive_less<std::wstring_view>{};
    EXPECT_TRUE(x(L"apple"s, L"BANANA"s));
}

TEST(WindowsStrings_Compare_wstring_view, Views2)
{
    auto const x = m::case_insensitive_less<std::wstring_view>{};
    EXPECT_TRUE(x(L"apple"s, L"BANANA"sv));
}
TEST(WindowsStrings_Compare_wstring_view, Views3)
{
    auto const x = m::case_insensitive_less<std::wstring_view>{};
    EXPECT_TRUE(x(L"apple"sv, L"BANANA"s));
}
TEST(WindowsStrings_Compare_wstring_view, Views4)
{
    auto const x = m::case_insensitive_less<std::wstring_view>{};
    EXPECT_TRUE(x(L"apple"sv, L"BANANA"sv));
}
