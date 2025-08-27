// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <chrono>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <thread>

#include <m/const_string/const_string.h>
#include <m/test_data/test_data.h>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(TestConstString, SimpleAssign) { auto x = m::make_wconst_string(L"foo"sv); }

TEST(TestConstString, SimpleAssignFromNullTerminated) { auto x = m::make_wconst_string(L"foo"); }

TEST(TestConstString, TryInitializerList)
{
    auto x = m::make_wconst_string(L"foo"sv);
    auto y = m::make_wconst_string(L"bar"sv);
    auto z = m::make_wconst_string({L"foo"sv, L"bar"sv, L"baz"sv});
}

TEST(TestConstString, TryInitializerListAndPrint)
{
    auto x = m::make_const_string("foo"sv);
    auto y = m::make_const_string("bar"sv);
    auto z = m::make_const_string({"foo"sv, "bar"sv, "baz"sv});

    std::println(
        "After all that, x = \"{}\", y = \"{}\", and z = \"{}\"", x->view(), y->view(), z->view());
}

TEST(TestConstString, TestAddWithNatoLetters1)
{
    auto x = m::make_const_string("foo"sv);

    for (auto const& e: m::test_data::nato_alphabet_sv)
    {
        auto t = m::make_const_string(e);
        // x = *x + *t;
    }

    std::println("{}", x->view());
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
