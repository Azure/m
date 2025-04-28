// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <array>
#include <iterator>
#include <string>
#include <string_view>

#include <m/multi_byte/multi_byte.h>

#include <Windows.h>

//
// Since CP_ACP is not a universal constant code page, we can't
// actually test any non-ASCII mappings.
//
// There are hopefully sufficient tests of the underlying multibyte
// API tests for anything interesting around encodings. Instead we
// will verify basic round tripping.
//

auto s_t1  = std::string("This is test data");
auto sv_t1 = std::string_view(s_t1);

auto ws_t1  = std::wstring(L"This is test data");
auto wsv_t1 = std::wstring_view(ws_t1);

auto u16sv_t1 =
    std::u16string_view(reinterpret_cast<char16_t const*>(wsv_t1.data()), wsv_t1.size());

TEST(AcpAPIs, acp_2_wstring)
{
    auto s = m::to_wstring_acp(s_t1);
    EXPECT_EQ(s, ws_t1);

    auto s2 = m::to_wstring_acp(sv_t1);
    EXPECT_EQ(s2, ws_t1);
}

TEST(AcpAPIs, acp_2_u16string)
{
    auto s = m::to_u16string_acp(s_t1);
    EXPECT_EQ(s, u16sv_t1);

    auto s2 = m::to_u16string_acp(sv_t1);
    EXPECT_EQ(s2, u16sv_t1);
}

TEST(AcpAPIs, wstring_2_acp)
{
    auto s1 = m::to_string_acp(ws_t1);
    EXPECT_EQ(s1, s_t1);

    auto s2 = m::to_string_acp(wsv_t1);
    EXPECT_EQ(s2, s_t1);
}

TEST(AcpAPIs, u16string_2_acp)
{
    auto s1 = m::to_string_acp(u16sv_t1);
    EXPECT_EQ(s1, s_t1);
}
