// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <string>
#include <string_view>

#include <m/formatters/FILE_NOTIFY_INFORMATION.h>

#include <Windows.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

//
// Copy of the definition of FILE_NOTIFY_EXTENDED_INFORMATION with
// the character array expanded so we can put data in it.
//

struct testingFILE_NOTIFY_INFORMATION
{
    DWORD         NextEntryOffset;
    DWORD         Action;
    DWORD         FileNameLength;
    WCHAR         FileName[255];
};

static_assert(offsetof(testingFILE_NOTIFY_INFORMATION, NextEntryOffset) ==
              offsetof(FILE_NOTIFY_INFORMATION, NextEntryOffset));

static_assert(offsetof(testingFILE_NOTIFY_INFORMATION, Action) ==
              offsetof(FILE_NOTIFY_INFORMATION, Action));

static_assert(offsetof(testingFILE_NOTIFY_INFORMATION, FileNameLength) ==
              offsetof(FILE_NOTIFY_INFORMATION, FileNameLength));

static_assert(offsetof(testingFILE_NOTIFY_INFORMATION, FileName) ==
              offsetof(FILE_NOTIFY_INFORMATION, FileName));

constexpr testingFILE_NOTIFY_INFORMATION info1 = {.NextEntryOffset = 0,
                                                           .Action          = FILE_ACTION_ADDED,
                                                           .FileNameLength = 20,
                                                           .FileName       = L"README.TXT"};

TEST(FILE_NOTIFY_INFORMATION, first)
{
    auto p = reinterpret_cast<FILE_NOTIFY_INFORMATION const*>(&info1);
    auto s = std::format(L"{}", *p);
    EXPECT_EQ(s, L"{ NextEntryOffset: 0, Action: FILE_ACTION_ADDED, FileName: \"README.TXT\" }"s);
}
