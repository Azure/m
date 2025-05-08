// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <filesystem>
#include <span>
#include <string_view>

#include <m/filesystem/filesystem.h>

using namespace std::string_view_literals;

TEST(store, first)
{
    // No test asserts, just execution.
    auto           p        = m::filesystem::make_path("temporary_file");
    constexpr auto contents = "Hello there filesystem!\n"sv;
    auto           bytes    = std::as_bytes(std::span(contents.begin(), contents.end()));

    m::filesystem::store(p, bytes);

    std::filesystem::remove(p);
}

