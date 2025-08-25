// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <chrono>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <print>

#include <m/const_string/const_string.h>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

auto test_strings = {"Alfa"s,   "Bravo"s,    "Charlie"s, "Delta"s,  "Echo"s,    "Foxtrot"s,
                     "Golf"s,   "Hotel"s,    "India"s,   "Juliet"s, "Kilo"s,    "Lima"s,
                     "Mike"s,   "November"s, "Oscar"s,   "Papa"s,   "Quebec"s,  "Romeo"s,
                     "Sierra"s, "Tango"s,    "Uniform"s, "Victor"s, "Whiskey"s, "X-Ray"s,
                     "Yankee"s, "Zulu"s};

TEST(TestConstString, SimpleAssign) { auto x = m::make_wconst_string(L"foo"sv); }

TEST(TestConstString, SimpleAssignFromNullTerminated) { auto x = m::make_wconst_string(L"foo"); }

TEST(TestConstString, TryInitializerList)
{
    auto             x = m::make_wconst_string(L"foo"sv);
    auto             y = m::make_wconst_string(L"bar"sv);
    auto             z = m::make_wconst_string({L"foo"sv, L"bar"sv, L"baz"sv});
}

TEST(TestConstString, TryInitializerListAndPrint)
{
    auto x = m::make_const_string("foo"sv);
    auto y = m::make_const_string("bar"sv);
    auto z = m::make_const_string({"foo"sv, "bar"sv, "baz"sv});

    std::println(
        "After all that, x = \"{}\", y = \"{}\", and z = \"{}\"", x->view(), y->view(), z->view());
}

#if 0
TEST(TestConstString, TryConcat)
{
    auto             x = m::make_wconst_string(L"foo"sv);
    auto             y = m::make_wconst_string(L"bar"sv);
    auto             z = m::wconst_string::concatenate(x, y);
    auto e = m::make_wconst_string(L"foobar"sv);
    auto c = m::wconst_string::compare(z, e);
    EXPECT_EQ(c, std::whatever::equal);
}
#endif
