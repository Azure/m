// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <filesystem>
#include <span>
#include <string_view>

#include <m/filesystem/filesystem.h>

using namespace std::string_view_literals;

TEST(path_casts, string)
{
    // No test asserts, just execution.
    auto p = m::filesystem::make_path("temporary_file");
    auto s = m::to<std::string>(p);
}

TEST(path_casts, wstring)
{
    // No test asserts, just execution.
    auto p = m::filesystem::make_path("temporary_file");
    auto s = m::to<std::wstring>(p);
}


TEST(path_casts, u8string)
{
    // No test asserts, just execution.
    auto p = m::filesystem::make_path("temporary_file");
    auto s = m::to<std::u8string>(p);
}

TEST(path_casts, u16string)
{
    // No test asserts, just execution.
    auto p = m::filesystem::make_path("temporary_file");
    auto s = m::to<std::u16string>(p);
}

TEST(path_casts, u32string)
{
    // No test asserts, just execution.
    auto p = m::filesystem::make_path("temporary_file");
    auto s = m::to<std::u32string>(p);
}
