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
#include <m/utf/utf_decode.h>

#include "test_data.h"

using namespace std::string_literals;
using namespace std::string_view_literals;

void
test_decode(utf_data_set const& data)
{
    auto it  = data.m_u8sv.begin();
    auto end = data.m_u8sv.end();

    // We'll follow the u32 data along and expect one char32_t to pop out for each one present

    std::size_t decode_count{};

    for (auto&& ch: data.m_u32chardata)
    {
        auto retval = m::utf::decode_utf8(it, end);
        EXPECT_EQ(retval.ch, ch);
        it = retval.it;
        decode_count++;
    }

    EXPECT_EQ(it, end);
}

TEST(DecodeUtf8, TestBasic) { test_decode(hellodata); }

TEST(DecodeUtf8, TestRFC3629_Example_1) { test_decode(rfc3629_ex_1); }

TEST(DecodeUtf8, TestRFC3629_Example_2) { test_decode(rfc3629_ex_2); }

TEST(DecodeUtf8, TestRFC3629_Example_3) { test_decode(rfc3629_ex_3); }

TEST(DecodeUtf8, TestRFC3629_Example_4) { test_decode(rfc3629_ex_4); }
