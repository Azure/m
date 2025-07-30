// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <string>
#include <string_view>

#include <m/formatters/HRESULT.h>

#include <Windows.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(HRESULT, format_S_OK)
{
    auto s = std::format(L"{}", fmtHRESULT{S_OK});
    EXPECT_EQ(s, L"S_OK"s);
}

TEST(HRESULT, format_S_FALSE)
{
    auto s = std::format(L"{}", fmtHRESULT{S_FALSE});
    EXPECT_EQ(s, L"S_FALSE"s);
}

TEST(HRESULT, format_E_FAIL)
{
    auto s = std::format(L"{}", fmtHRESULT{E_FAIL});
    EXPECT_EQ(s, L"E_FAIL"s);
}

TEST(HRESULT, unmapped_42)
{
    auto s = std::format(L"{}", fmtHRESULT(42));
    EXPECT_EQ(s, L"0x0000002a"s);
}

TEST(HRESULT, unmapped_HRESULT_MAX)
{
    auto s = std::format(L"{}", fmtHRESULT{(std::numeric_limits<HRESULT>::max)()});
    EXPECT_EQ(s, L"0x7fffffff"s);
}
