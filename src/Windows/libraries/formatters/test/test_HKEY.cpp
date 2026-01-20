// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <string>
#include <string_view>

#include <m/formatters/HKEY.h>

#include <Windows.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(HKEY, format_HKEY_CURRENT_USER)
{
    auto s = std::format(L"{}", HKEY_CURRENT_USER);
    EXPECT_EQ(s, L"HKEY_CURRENT_USER"s);
}

TEST(HKEY, format_HKEY_LOCAL_MACHINE)
{
    auto s = std::format(L"{}", HKEY_LOCAL_MACHINE);
    EXPECT_EQ(s, L"HKEY_LOCAL_MACHINE"s);
}

TEST(HKEY, format_default_hkey)
{
    auto s = std::format(L"{}", HKEY{});
    EXPECT_EQ(s, L"{hkey 0x0}"s);
}

