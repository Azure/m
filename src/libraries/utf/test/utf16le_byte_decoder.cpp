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
#include <m/utf/decode.h>

#include "test_data.h"

using namespace std::string_literals;
using namespace std::string_view_literals;

void
test_utf16le_byte_decode(utf_data_set const& data)
{
    // We'll follow the u32 data along and expect one char32_t to pop out for each one present

    std::size_t decode_count{};
    std::span<std::byte const> cursor = data.m_u16le_byte_data;

    for (auto&& ch: data.m_u32_chardata)
    {
        EXPECT_NE(cursor.size(), 0);
        auto const retval = m::utf::decode_utf16le(cursor);
        EXPECT_EQ(retval.m_char, ch);
        cursor = cursor.subspan(retval.m_offset);
        decode_count++;
    }

    EXPECT_EQ(cursor.size(), 0);
}

TEST(ByteDecodeUtf16le, TestBasic) { test_utf16le_byte_decode(hellodata); }

TEST(ByteDecodeUtf16le, TestEmpty) { test_utf16le_byte_decode(empty_data); }

TEST(ByteDecodeUtf16le, TestRFC3629_Example_1) { test_utf16le_byte_decode(rfc3629_ex_1); }

TEST(ByteDecodeUtf16le, TestRFC3629_Example_2) { test_utf16le_byte_decode(rfc3629_ex_2); }

TEST(ByteDecodeUtf16le, TestRFC3629_Example_3) { test_utf16le_byte_decode(rfc3629_ex_3); }

TEST(ByteDecodeUtf16le, TestRFC3629_Example_4) { test_utf16le_byte_decode(rfc3629_ex_4); }
