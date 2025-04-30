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

#include "test_data.h"

using namespace std::string_literals;
using namespace std::string_view_literals;

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
