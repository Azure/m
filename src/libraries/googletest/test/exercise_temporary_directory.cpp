// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

#include <m/googletest/temporary_directory.h>
#include <m/strings/convert.h>

TEST(TemporaryDirectory, MakeATempDir)
{
    auto const td = m::googletest::create_temporary_directory();
    auto const p  = td->path();

#if WIN32
    auto const n = p.native();
    auto const s = n.c_str();

    std::wcout << L"Got temporary path of \"" << m::to_wstring(s) << L"\"\n";
#else
    std::ignore = p;
#endif
}

TEST(TemporaryDirectory, RetainTrue)
{
    auto const td = m::googletest::create_temporary_directory();

    auto const p = td->path();

    std::filesystem::path child_path;

    {
        auto const tdchild = td->create(true); // create a child where we retain
        child_path         = tdchild->path();
        EXPECT_EQ(tdchild.use_count(), 1);
        // Allow tdchild to go out of scope; since we are retaining the path should still be there
    }

    EXPECT_TRUE(std::filesystem::exists(child_path));

    std::filesystem::remove_all(child_path);
}

TEST(TemporaryDirectory, RetainFalse)
{
    auto const td = m::googletest::create_temporary_directory();

    auto const p = td->path();

    std::filesystem::path child_path;

    {
        auto const tdchild = td->create(false); // create a child where we do not retain
        child_path         = tdchild->path();
        EXPECT_EQ(tdchild.use_count(), 1);
        // Allow tdchild to go out of scope
    }

    EXPECT_FALSE(std::filesystem::exists(child_path));
}
