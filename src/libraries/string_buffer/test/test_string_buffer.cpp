// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <chrono>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include <m/string_buffer/string_buffer.h>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(StringBuffer, FirstStringBuffer)
{
    m::string_buffer sb;
    //
}

TEST(StringBuffer, AppendToStringBuffer)
{
    m::string_buffer sb;

    sb.append("foo"sv);
    //
}

using small = m::basic_string_buffer<char, 4>;

TEST(StringBuffer, AppendBigToSmall)
{
    small sb;

    sb.append("12345"sv);

    EXPECT_STREQ(sb.c_str(), "12345");
}

TEST(StringBuffer, LargeNumberOfAppends)
{
    small       sb;
    std::string s;

    constexpr auto str = "12345"sv;

    for (std::size_t i = 0; i < 100; i++)
    {
        sb.append(str);
        s += str;
    }

    EXPECT_STREQ(sb.c_str(), s.c_str());
}

TEST(StringBuffer, LargeNumberOfTinyAppends)
{
    small       sb;
    std::string s;

    constexpr auto str = "12"sv;

    for (std::size_t i = 0; i < 500; i++)
    {
        sb.append(str);
        s += str;
    }

    EXPECT_STREQ(sb.c_str(), s.c_str());
}
TEST(StringBuffer, LargeNumberOfBigAppends)
{
    small       sb;
    std::string s;

    constexpr auto str = "1234567890abcdefghijklmnopqrstuvwxyz"sv;

    for (std::size_t i = 0; i < 100; i++)
    {
        sb.append(str);
        s += str;
    }

    EXPECT_STREQ(sb.c_str(), s.c_str());
}

TEST(StringBuffer, FormatIntoStringBuffer)
{
    m::string_buffer sb;

    auto it = std::back_inserter(sb);
    std::format_to(it, "Hello, {}, {}", "Mike", 42);

    EXPECT_STREQ(sb.c_str(), "Hello, Mike, 42");
}

TEST(StringBuffer, FormatIntoSmallStringBuffer)
{
    small sb;

    auto it = std::back_inserter(sb);
    std::format_to(it, "Hello, {}, {}", "Mike", 42);

    EXPECT_STREQ(sb.c_str(), "Hello, Mike, 42");
}

TEST(StringBuffer, TestAssign)
{
    m::string_buffer sb;

    sb.assign("Hello"sv);
    EXPECT_STREQ(sb.c_str(), "Hello");

    m::string_buffer sb2;

    sb2.assign(sb);
    EXPECT_STREQ(sb2.c_str(), "Hello");
}

TEST(StringBuffer, TestAssignEdgeCases)
{
    small sb1;
    small sb2;
    small sb3;

    sb1.assign("1234567890"sv);
    EXPECT_STREQ(sb1.c_str(), "1234567890");

    sb2.assign(sb1);
    EXPECT_STREQ(sb2.c_str(), "1234567890");

    sb1.assign("123"sv);
    EXPECT_STREQ(sb1.c_str(), "123");

    sb2.assign(sb1);
    EXPECT_STREQ(sb2.c_str(), "123");

    sb1.assign("1234567890"sv);
    EXPECT_STREQ(sb1.c_str(), "1234567890");

    sb1.assign(""sv);
    EXPECT_STREQ(sb1.c_str(), "");

    sb2.assign(""sv);
    EXPECT_STREQ(sb2.c_str(), "");
}
