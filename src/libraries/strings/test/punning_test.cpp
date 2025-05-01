// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/strings/convert.h>
#include <m/strings/punning.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(Punning, U8SVtoSV)
{
    std::u8string_view v1   = u8"hello"sv;
    auto               v2   = m::as_string_view(v1);
    constexpr bool     test = std::is_same_v<decltype(v2), std::string_view>;
    EXPECT_TRUE(test);
    static_assert(sizeof(decltype(v1)::value_type) == sizeof(decltype(v2)::value_type));
    EXPECT_EQ(reinterpret_cast<void const*>(v1.data()), reinterpret_cast<void const*>(v2.data()));
    EXPECT_EQ(v1.size(), v2.size());
}

TEST(Punning, U8SVtoU8SV)
{
    std::u8string_view v1   = u8"hello"sv;
    auto               v2   = m::as_u8string_view(v1);
    constexpr bool     test = std::is_same_v<decltype(v2), std::u8string_view>;
    EXPECT_TRUE(test);
    static_assert(sizeof(decltype(v1)::value_type) == sizeof(decltype(v2)::value_type));
    EXPECT_EQ(reinterpret_cast<void const*>(v1.data()), reinterpret_cast<void const*>(v2.data()));
    EXPECT_EQ(v1.size(), v2.size());
}

TEST(Punning, SVtoSV)
{
    std::string_view v1   = "hello"sv;
    auto             v2   = m::as_string_view(v1);
    constexpr bool   test = std::is_same_v<decltype(v2), std::string_view>;
    EXPECT_TRUE(test);
    static_assert(sizeof(decltype(v1)::value_type) == sizeof(decltype(v2)::value_type));
    EXPECT_EQ(reinterpret_cast<void const*>(v1.data()), reinterpret_cast<void const*>(v2.data()));
    EXPECT_EQ(v1.size(), v2.size());
}

TEST(Punning, SVtoU8SV)
{
    std::string_view v1   = "hello"sv;
    auto             v2   = m::as_u8string_view(v1);
    constexpr bool   test = std::is_same_v<decltype(v2), std::u8string_view>;
    EXPECT_TRUE(test);
    static_assert(sizeof(decltype(v1)::value_type) == sizeof(decltype(v2)::value_type));
    EXPECT_EQ(reinterpret_cast<void const*>(v1.data()), reinterpret_cast<void const*>(v2.data()));
    EXPECT_EQ(v1.size(), v2.size());
}
