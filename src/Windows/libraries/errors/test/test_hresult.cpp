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

TEST(HRESULT, format_S_OK)
{
    //
    // auto s = std::format(L"{}", fmtHRESULT{S_OK});
    // EXPECT_EQ(s, L"S_OK"s);
}
