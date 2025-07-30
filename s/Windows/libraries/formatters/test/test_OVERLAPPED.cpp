// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <string>
#include <string_view>

#include <m/formatters/OVERLAPPED.h>

#include <Windows.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

const OVERLAPPED data1 = {
    .Internal     = 0, // Actually NTSTATUS
    .InternalHigh = 0, // BytesTransferred
    .Pointer      = nullptr,
    .hEvent       = INVALID_HANDLE_VALUE,
};

TEST(OVERLAPPED, first)
{
    auto s = std::format(L"{}", data1);
    EXPECT_EQ(s, L"{ OVERLAPPED Status: STATUS_WAIT_0, BytesTransferred: 0, Offset: 0, Handle: 0xffffffffffffffff }"s);
}
