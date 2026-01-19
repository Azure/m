// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <string>
#include <string_view>

#include <Windows.h>

#include <m/windows_wrappers/win32_dword_ms.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(Win32DWORDMs, First)
{
    DWORD             ms = 100;
    m::win32_dword_ms x(ms);

    EXPECT_EQ(static_cast<DWORD>(x), 100);
}
