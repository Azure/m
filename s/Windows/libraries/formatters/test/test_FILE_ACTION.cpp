// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <string>
#include <string_view>

#include <m/formatters/FILE_ACTION.h>

#include <Windows.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(FILE_ACTION, format_FILE_ACTION_ADDED)
{
    auto s = std::format(L"{}", fmtFILE_ACTION{FILE_ACTION_ADDED});
    EXPECT_EQ(s, L"FILE_ACTION_ADDED"s);
}

TEST(FILE_ACTION, format_FILE_ACTION_REMOVED)
{
    auto s = std::format(L"{}", fmtFILE_ACTION{FILE_ACTION_REMOVED});
    EXPECT_EQ(s, L"FILE_ACTION_REMOVED"s);
}

TEST(FILE_ACTION, format_FILE_ACTION_MODIFIED)
{
    auto s = std::format(L"{}", fmtFILE_ACTION{FILE_ACTION_MODIFIED});
    EXPECT_EQ(s, L"FILE_ACTION_MODIFIED"s);
}

TEST(FILE_ACTION, format_FILE_ACTION_RENAMED_OLD_NAME)
{
    auto s = std::format(L"{}", fmtFILE_ACTION{FILE_ACTION_RENAMED_OLD_NAME});
    EXPECT_EQ(s, L"FILE_ACTION_RENAMED_OLD_NAME"s);
}

TEST(FILE_ACTION, format_FILE_ACTION_RENAMED_NEW_NAME)
{
    auto s = std::format(L"{}", fmtFILE_ACTION{FILE_ACTION_RENAMED_NEW_NAME});
    EXPECT_EQ(s, L"FILE_ACTION_RENAMED_NEW_NAME"s);
}

TEST(FILE_ACTION, unmapped_0)
{
    auto s = std::format(L"{}", fmtFILE_ACTION{0});
    EXPECT_EQ(s, L"Unmapped action 0"s);
}

TEST(FILE_ACTION, unmapped_DWORD_MAX)
{
    auto s = std::format(L"{}", fmtFILE_ACTION{(std::numeric_limits<DWORD>::max)()});
    EXPECT_EQ(s, L"Unmapped action 4294967295"s);
}






