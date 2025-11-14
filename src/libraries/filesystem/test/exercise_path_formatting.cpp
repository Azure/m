// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <filesystem>
#include <span>
#include <string_view>

#include <m/filesystem/filesystem.h>

using namespace std::string_view_literals;

TEST(path_formatting, wide_formatting)
{
    auto p = m::filesystem::make_path("abc123");
    // Unfortunately these tests are platform dependent.
    auto s = std::format(L"{}", p);
    EXPECT_EQ(s, L"abc123");
}

//
// There used to be a "narrow formatting" test but it has been removed.
// 
// This is because formatting to "char" requires platofrm-specific encoding. On
// Windows, char == CP_ACP, on Linux, char == UTF-8 and we're not hooked up here
// sufficiently to make the path formatting go through this transcoding.
//