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

        EXPECT_THROW(auto y = m::utf::decode_utf8(it, end), m::utf::utf_sequence_truncated_error);
    }
} // namespace

TEST(ValidateTruncatedUtf8Handling, MalformedC0) { test_decode(utf8_malformed_c0); }
TEST(ValidateTruncatedUtf8Handling, MalformedC1) { test_decode(utf8_malformed_c1); }

TEST(ValidateTruncatedUtf8Handling, Truncated2ByteSequence) { test_decode(utf8_trunc_2b_1b); }

TEST(ValidateTruncatedUtf8Handling, Truncated3ByteSequence_1Byte) { test_decode(utf8_trunc_3b_1b); }

TEST(ValidateTruncatedUtf8Handling, Truncated3ByteSequence_2Bytes)
{
    test_decode(utf8_trunc_3b_2b);
}

TEST(ValidateTruncatedUtf8Handling, Truncated4ByteSequence_1Byte) { test_decode(utf8_trunc_4b_1b); }

TEST(ValidateTruncatedUtf8Handling, Truncated4ByteSequence_2Bytes)
{
    test_decode(utf8_trunc_4b_2b);
}

TEST(ValidateTruncatedUtf8Handling, Truncated4ByteSequence_3Bytes)
{
    test_decode(utf8_trunc_4b_3b);
}

// Do more probably? More combinations, perhaps random data?
