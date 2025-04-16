// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <array>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#include <m/strings/convert.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

static std::array<char8_t, 5> u8_hello{'h', 'e', 'l', 'l', 'o'};
static auto                   u8_hello_sv = std::u8string_view(u8_hello.data(), u8_hello.size());

static std::array<char32_t, 5> u32_hello{'h', 'e', 'l', 'l', 'o'};
static auto u32_hello_sv = std::u32string_view(u32_hello.data(), u32_hello.size());

TEST(CvtU32ToU8, TestBasic)
{
    auto x = m::to_u8string(u32_hello_sv);

    EXPECT_EQ(u8_hello_sv.compare(x), 0);
    //
}