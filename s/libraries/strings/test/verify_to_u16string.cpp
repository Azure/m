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

TEST(VerifyConvertToU16, NotEmptyIntoBasicString)
{
    std::u16string s;
    auto const     sv = u"Hello, world!"sv;

    m::to_u16string(sv, s);

    EXPECT_EQ(s, sv);
}

TEST(VerifyConvertToU16, NotEmptyToBasicString)
{
    auto const sv = u"Hello, world!"sv;
    auto const s  = m::to_u16string(sv);

    EXPECT_EQ(s, sv);
}

TEST(VerifyConvertToU16, EmptyIntoBasicString)
{
    std::u16string s;
    auto const     sv = u""sv;

    m::to_u16string(sv, s);

    EXPECT_EQ(s, sv);
}

TEST(VerifyConvertToU16, EmptyToBasicString)
{
    auto const sv = u""sv;
    auto const s  = m::to_u16string(sv);

    EXPECT_EQ(s, sv);
}

void
verify_to_u16string_convert(utf_data_set const& data)
{
    auto const ref = data.m_u16le_sv;

    std::u16string result1;
    m::to_u16string(data.m_u8_sv, result1);
    EXPECT_EQ(ref, result1);

    auto result2 = m::to_u16string(data.m_u8_sv);
    EXPECT_EQ(ref, result2);

    std::u16string result3;
    m::to_u16string(data.m_u16le_sv, result3);
    EXPECT_EQ(ref, result3);

    auto result4 = m::to_u16string(data.m_u16le_sv);
    EXPECT_EQ(ref, result4);

    std::u16string result5;
    m::to_u16string(data.m_u32_sv, result5);
    EXPECT_EQ(ref, result5);

    auto result6 = m::to_u16string(data.m_u32_sv);
    EXPECT_EQ(ref, result6);
}

TEST(VerifyConvertToU16, TestEmpty) { verify_to_u16string_convert(empty_data); }

TEST(VerifyConvertToU16, TestBasic) { verify_to_u16string_convert(hellodata); }

TEST(VerifyConvertToU16, TestRFC3629_Example_1) { verify_to_u16string_convert(rfc3629_ex_1); }

TEST(VerifyConvertToU16, TestRFC3629_Example_2) { verify_to_u16string_convert(rfc3629_ex_2); }

TEST(VerifyConvertToU16, TestRFC3629_Example_3) { verify_to_u16string_convert(rfc3629_ex_3); }

TEST(VerifyConvertToU16, TestRFC3629_Example_4) { verify_to_u16string_convert(rfc3629_ex_4); }
