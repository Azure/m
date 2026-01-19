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

#include <m/sstring/sstring.h>
#include <m/strings/compare.h>
#include <m/test_data/test_data.h>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(TestSString, SimpleAssign) { m::wsstring x{L"foo"}; }

TEST(TestSString, TryConcat)
{
    m::wsstring x{L"foo"sv};
    m::wsstring y{L"bar"sv};
    auto        z = x + y;
    m::wsstring e(L"foobar"sv);

    EXPECT_EQ(z, e);
}

TEST(TestSString, TestAddWithNatoLetters1)
{
    auto x = m::sstring("foo"sv);

    for (auto const& e: m::test_data::nato_alphabet_sv)
    {
        // auto t = m::make_const_string(e);
        x = x + e;
    }

    EXPECT_EQ(
        x,
        "fooAlfaBravoCharlieDeltaEchoFoxtrotGolfHotelIndiaJuliettKiloLimaMikeNovemberOscarPapaQuebecRomeoSierraTangoUniformVictorWhiskeyXrayYankeeZulu");
}

TEST(TestSString, TestSubstr)
{
    auto x = m::sstring("foo"sv);

    for (auto const& e: m::test_data::nato_alphabet_sv)
    {
        // auto t = m::make_const_string(e);
        x = x + e;
    }

    auto y = x.substr(20, 5);
    EXPECT_EQ(y, "eltaE");
}

TEST(TestSString, TestLeft)
{
    auto x = m::sstring(m::test_data::alpha_num_sv);
    auto y = x.left(5);
    EXPECT_EQ(y, "abcde");
}

TEST(TestSString, TestRight)
{
    auto x = m::sstring(m::test_data::alpha_num_sv);
    auto y = x.right(7);
    EXPECT_EQ(y, "3456789");
}

TEST(TestSString, TestCaseInsensitiveLess)
{
    auto x = m::sstring(m::test_data::alpha_num_sv);
    auto y = std::string("flarb");

    auto z = m::case_insensitive_less<m::sstring>{};
    EXPECT_EQ(z(y, x), false);
}
