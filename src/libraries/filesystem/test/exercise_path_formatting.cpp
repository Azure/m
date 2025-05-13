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

TEST(path_formatting, narrow_formatting)
{
    auto p = m::filesystem::make_path("abc123");
    auto s = std::format("{}", p);
    EXPECT_EQ(s, "abc123");
}
