// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#include <m/strings/convert.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(StringConvertTests, TestToString)
{
    std::string s;

    s = m::to_string("foo"sv);

    EXPECT_EQ(s, "foo"sv);
}

TEST(StringConvertTests, TestWToString)
{
    std::string s;

    s = m::to_string(L"foo"sv);

    EXPECT_EQ(s, "foo"sv);
}

TEST(StringConvertTests, TestToWString)
{
    std::wstring s;

    s = m::to_wstring("foo"sv);

    EXPECT_EQ(s, L"foo"sv);
}

TEST(StringConvertTests, TestWToWString)
{
    std::wstring s;

    s = m::to_wstring(L"foo"sv);

    EXPECT_EQ(s, L"foo"sv);
}
