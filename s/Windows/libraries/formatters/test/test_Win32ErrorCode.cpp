// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <string>
#include <string_view>

#include <m/formatters/Win32ErrorCode.h>

#include <Windows.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(Win32ErrorCode, format_ERROR_INVALID_FUNCTION)
{
    auto s = std::format(L"{}", fmtWin32ErrorCode{ERROR_INVALID_FUNCTION});
    EXPECT_EQ(s, L"{ ERROR_INVALID_FUNCTION (1) }"s);
}

TEST(Win32ErrorCode, format_ERROR_FILE_NOT_FOUND)
{
    auto s = std::format(L"{}", fmtWin32ErrorCode{ERROR_FILE_NOT_FOUND});
    EXPECT_EQ(s, L"{ ERROR_FILE_NOT_FOUND (2) }"s);
}

TEST(Win32ErrorCode, format_ERROR_PATH_NOT_FOUND)
{
    auto s = std::format(L"{}", fmtWin32ErrorCode{ERROR_PATH_NOT_FOUND});
    EXPECT_EQ(s, L"{ ERROR_PATH_NOT_FOUND (3) }"s);
}

TEST(Win32ErrorCode, format_ERROR_ACCESS_DENIED)
{
    auto s = std::format(L"{}", fmtWin32ErrorCode{ERROR_ACCESS_DENIED});
    EXPECT_EQ(s, L"{ ERROR_ACCESS_DENIED (5) }"s);
}

TEST(Win32ErrorCode, unmapped_42)
{
    auto s = std::format(L"{}", fmtWin32ErrorCode(42));
    EXPECT_EQ(s, L"{ 42 }"s);
}

TEST(Win32ErrorCode, unmapped_DWORD_MAX)
{
    auto s = std::format(L"{}", fmtWin32ErrorCode{(std::numeric_limits<DWORD>::max)()});
    EXPECT_EQ(s, L"{ 4294967295 }"s);
}






