// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <string>
#include <string_view>

#include <m/errors/errors.h>
#include <m/errors/hresult.h>
#include <m/exception/exception.h>

#include <Windows.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(FILE_ACTION, format_FILE_ACTION_ADDED)
{
    // auto s = std::format(L"{}", fmtFILE_ACTION{FILE_ACTION_ADDED});
    // EXPECT_EQ(s, L"FILE_ACTION_ADDED"s);
}
