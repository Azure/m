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
#include <m/utf/exceptions.h>
#include <m/utf/decode.h>

#include "test_data.h"

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace
{
    template <std::size_t N>
    void
    test_decode(std::array<char8_t, N> const& data, std::size_t decodes_before_exception = 0)
    {
        auto it  = data.begin();
        auto end = data.end();

        while (decodes_before_exception > 0)
        {
            auto x = m::utf::decode_utf8(it, end);
            it     = x.it;
            EXPECT_NE(it, end);
        }

        EXPECT_THROW(auto y = m::utf::decode_utf8(it, end), m::utf::utf_invalid_encoding_error);
    }
} // namespace

TEST(ValidateNonShortestUtf8Handling, Invalid2ByteSequence_1) { test_decode(utf8_nonshortest_2b_1); }

TEST(ValidateNonShortestUtf8Handling, Invalid3ByteSequence_1) { test_decode(utf8_nonshortest_3b_1); }

TEST(ValidateNonShortestUtf8Handling, Invalid4ByteSequence_1) { test_decode(utf8_nonshortest_4b_1); }
