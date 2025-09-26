// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <chrono>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <thread>

#include <m/string_buffer/string_buffer.h>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

using pbuff = m::pooled_string_buffer;
using pool_t = typename pbuff::pool_type;

TEST(PooledStringBuffer, FirstStringBuffer)
{
    auto pool = std::make_shared<pool_t>();
    pbuff sb(pool);
}

TEST(PooledStringBuffer, AppendToStringBuffer)
{
    auto             pool = std::make_shared<pool_t>();
    pbuff            sb(pool);
    sb.append("foo"sv);
}

using small = m::basic_pooled_string_buffer<char, 4>;

TEST(PooledStringBuffer, AppendBigToSmall)
{
    auto  pool = std::make_shared<pool_t>();
    small sb(pool);

    sb.append("12345"sv);

    EXPECT_STREQ(sb.c_str(), "12345");
}

TEST(PooledStringBuffer, LargeNumberOfAppends)
{
    auto        pool = std::make_shared<pool_t>();
    small       sb(pool);
    std::string s;

    constexpr auto str = "12345"sv;

    for (std::size_t i = 0; i < 400; i++)
    {
        sb.append(str);
        s += str;
    }

    EXPECT_STREQ(sb.c_str(), s.c_str());
}

TEST(PooledStringBuffer, LargeNumberOfTinyAppends)
{
    auto        pool = std::make_shared<pool_t>();
    small       sb(pool);
    std::string s;

    constexpr auto str = "12"sv;

    for (std::size_t i = 0; i < 1500; i++)
    {
        sb.append(str);
        s += str;
    }

    EXPECT_STREQ(sb.c_str(), s.c_str());
}
TEST(PooledStringBuffer, LargeNumberOfBigAppends)
{
    auto        pool = std::make_shared<pool_t>();
    small       sb(pool);
    std::string s;

    constexpr auto str = "1234567890abcdefghijklmnopqrstuvwxyz"sv;

    for (std::size_t i = 0; i < 400; i++)
    {
        sb.append(str);
        s += str;
    }

    EXPECT_STREQ(sb.c_str(), s.c_str());
}

TEST(PooledStringBuffer, FormatIntoStringBuffer)
{
    auto  pool = std::make_shared<pool_t>();
    pbuff sb(pool);

    auto it = std::back_inserter(sb);
    std::format_to(it, "Hello, {}, {}", "Mike", 42);

    EXPECT_STREQ(sb.c_str(), "Hello, Mike, 42");
}

TEST(PooledStringBuffer, FormatIntoSmallStringBuffer)
{
    auto  pool = std::make_shared<pool_t>();
    small sb(pool);

    auto it = std::back_inserter(sb);
    std::format_to(it, "Hello, {}, {}", "Mike", 42);

    EXPECT_STREQ(sb.c_str(), "Hello, Mike, 42");
}

TEST(PooledStringBuffer, TestAssign)
{
    auto  pool = std::make_shared<pool_t>();
    pbuff sb(pool);

    sb.assign("Hello"sv);
    EXPECT_STREQ(sb.c_str(), "Hello");

    pbuff sb2(pool);

    sb2.assign(sb);
    EXPECT_STREQ(sb2.c_str(), "Hello");
}

TEST(PooledStringBuffer, TestAssignEdgeCases)
{
    auto  pool = std::make_shared<pool_t>();

    small sb1(pool);
    small sb2(pool);
    small sb3(pool);

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
