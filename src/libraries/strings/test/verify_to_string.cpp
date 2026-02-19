// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/strings/convert.h>
#include <m/utility/pointers.h>

#include "test_data.h"

using namespace std::string_literals;
using namespace std::string_view_literals;

[[maybe_unused]] static char8_t const* pu8c_nullptr    = nullptr;
[[maybe_unused]] static char8_t const* pu8c_notnullptr = u8"foo";

//
// Verify the optionality mechanics are kicking in
//

static_assert(std::is_same_v<decltype(m::to_u8string(pu8c_nullptr)), std::u8string>);
static_assert(
    std::is_same_v<decltype(m::to_u8string(m::not_null(pu8c_notnullptr))), std::u8string>);

//
// Unfortunately in the multi-platform world, there's essentially nothing
// that basic_string<char> / basic_string_view<char> can convert to, so we
// can only perform the most basic tests that the "null" conversions
// work and that empty strings don't fail etc.
//

TEST(VerifyBasicStringChar, NotEmptyIntoBasicString)
{
    std::string s;
    auto const  sv = "Hello, world!"sv;

    m::to_string(sv, s);

    EXPECT_EQ(s, sv);
}

TEST(VerifyBasicStringChar, NotEmptyToBasicString)
{
    auto const sv = "Hello, world!"sv;
    auto const s  = m::to_string(sv);

    EXPECT_EQ(s, sv);
}

TEST(VerifyBasicStringChar, EmptyIntoBasicString)
{
    std::string s;
    auto const  sv = ""sv;

    m::to_string(sv, s);

    EXPECT_EQ(s, sv);
}

TEST(VerifyBasicStringChar, EmptyToBasicString)
{
    auto const sv = ""sv;
    auto const s  = m::to_string(sv);

    EXPECT_EQ(s, sv);
}
