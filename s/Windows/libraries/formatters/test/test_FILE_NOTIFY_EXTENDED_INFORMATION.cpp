// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <string>
#include <string_view>

#include <m/formatters/FILE_NOTIFY_EXTENDED_INFORMATION.h>

#include <Windows.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

//
// Copy of the definition of FILE_NOTIFY_EXTENDED_INFORMATION with
// the character array expanded so we can put data in it.
//

struct testingFILE_NOTIFY_EXTENDED_INFORMATION
{
    DWORD         NextEntryOffset;
    DWORD         Action;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastModificationTime;
    LARGE_INTEGER LastChangeTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER AllocatedLength;
    LARGE_INTEGER FileSize;
    DWORD         FileAttributes;
    union
    {
        DWORD ReparsePointTag;
        DWORD EaSize;
    } SomeUnion;
    LARGE_INTEGER FileId;
    LARGE_INTEGER ParentFileId;
    DWORD         FileNameLength;
    WCHAR         FileName[255];
};

static_assert(offsetof(testingFILE_NOTIFY_EXTENDED_INFORMATION, NextEntryOffset) ==
              offsetof(FILE_NOTIFY_EXTENDED_INFORMATION, NextEntryOffset));

static_assert(offsetof(testingFILE_NOTIFY_EXTENDED_INFORMATION, Action) ==
              offsetof(FILE_NOTIFY_EXTENDED_INFORMATION, Action));

static_assert(offsetof(testingFILE_NOTIFY_EXTENDED_INFORMATION, CreationTime) ==
              offsetof(FILE_NOTIFY_EXTENDED_INFORMATION, CreationTime));

static_assert(offsetof(testingFILE_NOTIFY_EXTENDED_INFORMATION, LastModificationTime) ==
              offsetof(FILE_NOTIFY_EXTENDED_INFORMATION, LastModificationTime));

static_assert(offsetof(testingFILE_NOTIFY_EXTENDED_INFORMATION, LastChangeTime) ==
              offsetof(FILE_NOTIFY_EXTENDED_INFORMATION, LastChangeTime));

static_assert(offsetof(testingFILE_NOTIFY_EXTENDED_INFORMATION, LastAccessTime) ==
              offsetof(FILE_NOTIFY_EXTENDED_INFORMATION, LastAccessTime));

static_assert(offsetof(testingFILE_NOTIFY_EXTENDED_INFORMATION, AllocatedLength) ==
              offsetof(FILE_NOTIFY_EXTENDED_INFORMATION, AllocatedLength));

static_assert(offsetof(testingFILE_NOTIFY_EXTENDED_INFORMATION, FileSize) ==
              offsetof(FILE_NOTIFY_EXTENDED_INFORMATION, FileSize));

static_assert(offsetof(testingFILE_NOTIFY_EXTENDED_INFORMATION, FileAttributes) ==
              offsetof(FILE_NOTIFY_EXTENDED_INFORMATION, FileAttributes));

static_assert(offsetof(testingFILE_NOTIFY_EXTENDED_INFORMATION, SomeUnion.ReparsePointTag) ==
              offsetof(FILE_NOTIFY_EXTENDED_INFORMATION, ReparsePointTag));

static_assert(offsetof(testingFILE_NOTIFY_EXTENDED_INFORMATION, FileId) ==
              offsetof(FILE_NOTIFY_EXTENDED_INFORMATION, FileId));

static_assert(offsetof(testingFILE_NOTIFY_EXTENDED_INFORMATION, ParentFileId) ==
              offsetof(FILE_NOTIFY_EXTENDED_INFORMATION, ParentFileId));

static_assert(offsetof(testingFILE_NOTIFY_EXTENDED_INFORMATION, FileNameLength) ==
              offsetof(FILE_NOTIFY_EXTENDED_INFORMATION, FileNameLength));

static_assert(offsetof(testingFILE_NOTIFY_EXTENDED_INFORMATION, FileName) ==
              offsetof(FILE_NOTIFY_EXTENDED_INFORMATION, FileName));

constexpr LARGE_INTEGER some_file_time = {.LowPart = 0, .HighPart = 0x01f00000};

constexpr LARGE_INTEGER large_integer_zero = {.QuadPart = 0};

constexpr testingFILE_NOTIFY_EXTENDED_INFORMATION info1 = {.NextEntryOffset = 0,
                                                           .Action          = FILE_ACTION_ADDED,
                                                           .CreationTime    = some_file_time,
                                                           .LastModificationTime = some_file_time,
                                                           .LastChangeTime       = some_file_time,
                                                           .LastAccessTime  = large_integer_zero,
                                                           .AllocatedLength = large_integer_zero,
                                                           .FileSize        = large_integer_zero,
                                                           .FileAttributes  = 0,
                                                           .SomeUnion      = {.ReparsePointTag = 0},
                                                           .FileId         = large_integer_zero,
                                                           .ParentFileId   = large_integer_zero,
                                                           .FileNameLength = 20,
                                                           .FileName       = L"README.TXT"};

TEST(FILE_NOTIFY_EXTENDED_INFORMATION, first)
{
    auto p = reinterpret_cast<FILE_NOTIFY_EXTENDED_INFORMATION const*>(&info1);
    auto s = std::format(L"{}", *p);
    EXPECT_EQ(
        s,
        L"{ NextEntryOffset: 0, Action: FILE_ACTION_ADDED, CreationTime: { Su 2043-05-31 11:40:11.040 }, LastModificationTime: { Su 2043-05-31 11:40:11.040 }, LastChangeTime: { Su 2043-05-31 11:40:11.040 }, LastAccessTime: { Mo 1601-01-01 00:00:00.000 }, AllocatedLength: 0, FileSize: 0, FileAttributes: 0, ReparsePointTag: 0, FileId: 0, ParentFileId: 0, FileName: \"README.TXT\" }"s);
}
