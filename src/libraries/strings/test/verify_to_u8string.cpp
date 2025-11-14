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

TEST(VerifyConvertToU8, NotEmptyIntoBasicString)
{
    std::u8string s;
    auto const    sv = u8"Hello, world!"sv;

    m::to_u8string(sv, s);

    EXPECT_EQ(s, sv);
}

TEST(VerifyConvertToU8, NotEmptyToBasicString)
{
    auto const sv = u8"Hello, world!"sv;
    auto const s  = m::to_u8string(sv);

    EXPECT_EQ(s, sv);
}

TEST(VerifyConvertToU8, EmptyIntoBasicString)
{
    std::u8string s;
    auto const    sv = u8""sv;

    m::to_u8string(sv, s);

    EXPECT_EQ(s, sv);
}

TEST(VerifyConvertToU8, EmptyToBasicString)
{
    auto const sv = u8""sv;
    auto const s  = m::to_u8string(sv);

    EXPECT_EQ(s, sv);
}

void
verify_to_u8string_convert(utf_data_set const& data)
{
    auto const    ref = data.m_u8_sv;

    std::u8string result1;
    m::to_u8string(data.m_u8_sv, result1);
    EXPECT_EQ(ref, result1);

    auto result2 = m::to_u8string(data.m_u8_sv);
    EXPECT_EQ(ref, result2);

    std::u8string result3;
    m::to_u8string(data.m_u16le_sv, result3);
    EXPECT_EQ(ref, result3);

    auto result4 = m::to_u8string(data.m_u16le_sv);
    EXPECT_EQ(ref, result4);

    std::u8string result5;
    m::to_u8string(data.m_u32_sv, result5);
    EXPECT_EQ(ref, result5);

    auto result6 = m::to_u8string(data.m_u32_sv);
    EXPECT_EQ(ref, result6);

    auto r7 = m::to_u8string(data.m_ou8_sv);
    EXPECT_TRUE(r7.has_value());
    EXPECT_EQ(ref, r7.value());

    auto r8 = m::to_u8string(data.m_ou16le_sv);
    EXPECT_TRUE(r8.has_value());
    EXPECT_EQ(ref, r8.value());

    auto r9 = m::to_u8string(data.m_ou32_sv);
    EXPECT_TRUE(r9.has_value());
    EXPECT_EQ(ref, r9.value());

    std::optional<std::u8string> ou8s;
    std::optional<std::u16string> ou16s;
    std::optional<std::u32string> ou32s;

    EXPECT_FALSE(ou8s.has_value());
    EXPECT_FALSE(ou16s.has_value());
    EXPECT_FALSE(ou32s.has_value());

    auto r10 = m::to_u8string(ou8s);
    EXPECT_FALSE(r10.has_value());

    auto r11 = m::to_u8string(ou16s);
    EXPECT_FALSE(r11.has_value());

    auto r12 = m::to_u8string(ou32s);
    EXPECT_FALSE(r12.has_value());
}

TEST(VerifyConvertToU8, TestEmpty) { verify_to_u8string_convert(empty_data); }

TEST(VerifyConvertToU8, TestBasic) { verify_to_u8string_convert(hellodata); }

TEST(VerifyConvertToU8, TestRFC3629_Example_1) { verify_to_u8string_convert(rfc3629_ex_1); }

TEST(VerifyConvertToU8, TestRFC3629_Example_2) { verify_to_u8string_convert(rfc3629_ex_2); }

TEST(VerifyConvertToU8, TestRFC3629_Example_3) { verify_to_u8string_convert(rfc3629_ex_3); }

TEST(VerifyConvertToU8, TestRFC3629_Example_4) { verify_to_u8string_convert(rfc3629_ex_4); }
