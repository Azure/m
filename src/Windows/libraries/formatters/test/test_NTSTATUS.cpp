// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <string>
#include <string_view>

#include <m/formatters/NTSTATUS.h>

#include <Windows.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(NTSTATUS, format_STATUS_PENDING)
{
    auto s = std::format(L"{}", fmtNTSTATUS{STATUS_PENDING});
    EXPECT_EQ(s, L"STATUS_PENDING"s);
}

TEST(NTSTATUS, format_STATUS_WAIT_0)
{
    auto s = std::format(L"{}", fmtNTSTATUS{STATUS_WAIT_0});
    EXPECT_EQ(s, L"STATUS_WAIT_0"s);
}

TEST(NTSTATUS, format_STATUS_ABANDONED_WAIT_0)
{
    auto s = std::format(L"{}", fmtNTSTATUS{STATUS_ABANDONED_WAIT_0});
    EXPECT_EQ(s, L"STATUS_ABANDONED_WAIT_0"s);
}

TEST(NTSTATUS, format_STATUS_INVALID_HANDLE)
{
    auto s = std::format(L"{}", fmtNTSTATUS{STATUS_INVALID_HANDLE});
    EXPECT_EQ(s, L"STATUS_INVALID_HANDLE"s);
}

TEST(NTSTATUS, format_STATUS_INVALID_PARAMETER)
{
    auto s = std::format(L"{}", fmtNTSTATUS{STATUS_INVALID_PARAMETER});
    EXPECT_EQ(s, L"STATUS_INVALID_PARAMETER"s);
}

TEST(NTSTATUS, unmapped_42)
{
    auto s = std::format(L"{}", fmtNTSTATUS(42));
    EXPECT_EQ(s, L"0x0000002a"s);
}

TEST(NTSTATUS, unmapped_DWORD_MAX)
{
    auto s = std::format(L"{}", fmtNTSTATUS{(std::numeric_limits<NTSTATUS>::max)()});
    EXPECT_EQ(s, L"0x7fffffff"s);
}






