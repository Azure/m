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
verify_utf32_decode(utf_data_set const& data)
{
    auto it  = data.m_u32_sv.begin();
    auto end = data.m_u32_sv.end();

    // We'll follow the u32 data along and expect one char32_t to pop out for each one present

    //
    // This may seem redundant, but in one case, we're going straight against
    // the string view and in the other we're going through the templatized
    // machinery so we are verifying that it is working.
    //

    for (auto&& ch: data.m_u32_chardata)
    {
        auto retval = m::utf::decode_utf32(it, end);
        EXPECT_EQ(retval.ch, ch);
        it = retval.it;
    }

    EXPECT_EQ(it, end);
}

TEST(DecodeUtf32, TestEmpty) { verify_utf32_decode(empty_data); }

TEST(DecodeUtf32, TestBasic) { verify_utf32_decode(hellodata); }

TEST(DecodeUtf32, TestRFC3629_Example_1) { verify_utf32_decode(rfc3629_ex_1); }

TEST(DecodeUtf32, TestRFC3629_Example_2) { verify_utf32_decode(rfc3629_ex_2); }

TEST(DecodeUtf32, TestRFC3629_Example_3) { verify_utf32_decode(rfc3629_ex_3); }

TEST(DecodeUtf32, TestRFC3629_Example_4) { verify_utf32_decode(rfc3629_ex_4); }
