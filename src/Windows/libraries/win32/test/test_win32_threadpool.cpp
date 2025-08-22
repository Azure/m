// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <string>
#include <string_view>

#include <Windows.h>

#include <m/win32/threadpool.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(Win32Threadpool, First)
{
    EXPECT_EQ(static_cast<DWORD>(100), 100);
}


