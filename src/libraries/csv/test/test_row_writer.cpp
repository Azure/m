// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <array>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#include <m/csv/writer.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(RowWriterTests, TestThreeAnimals)
{
    std::u8string s;
    auto          iter = std::back_inserter(s);

    std::array<std::string, 3> strings{"dog"s, "cat"s, "mouse"s};

    auto writer = m::csv::writer([&](auto spn) { iter = std::ranges::copy(spn, iter).out; });

    writer.write_row(strings);

    EXPECT_EQ(s, u8"dog,cat,mouse\r\n"s);
}

TEST(RowWriterTests, TestBlankColumn)
{
    std::u8string s;
    auto          iter = std::back_inserter(s);

    std::array<std::u8string, 3> strings{u8"dog"s, u8""s, u8"mouse"s};

    auto writer = m::csv::writer([&](auto spn) { iter = std::ranges::copy(spn, iter).out; });

    writer.write_row(strings);

    EXPECT_EQ(s, u8"dog,,mouse\r\n"s);
}

TEST(RowWriterTests, TestColumnWithSpace)
{
    std::u8string s;
    auto          iter = std::back_inserter(s);

    std::array<std::u8string, 3> strings{u8"dog"s, u8"cat"s, u8"mouse with cheese"s};

    auto writer = m::csv::writer([&](auto spn) { iter = std::ranges::copy(spn, iter).out; });

    writer.write_row(strings);

    EXPECT_EQ(s, u8"dog,cat,mouse with cheese\r\n"s);
}

TEST(RowWriterTests, TestABunchOfBlankColumns)
{
    std::u8string s;
    auto          iter = std::back_inserter(s);

    std::array<std::u8string, 5> strings{u8""s, u8""s, u8""s, u8""s, u8""s};

    auto writer = m::csv::writer([&](auto spn) { iter = std::ranges::copy(spn, iter).out; });

    writer.write_row(strings);

    EXPECT_EQ(s, u8",,,,\r\n"s);
}

TEST(RowWriterTests, TestThreeAnimalsWithQuotes)
{
    std::u8string s;
    auto          iter = std::back_inserter(s);

    std::array<std::u8string, 3> strings{
        u8"dog called \"Fido\""s, u8"cat called \"Felix\""s, u8"mouse called \"Tiny\""s};

    auto writer = m::csv::writer([&](auto spn) { iter = std::ranges::copy(spn, iter).out; });

    writer.write_row(strings);

    EXPECT_EQ(
        s,
        u8"\"dog called \"\"Fido\"\"\",\"cat called \"\"Felix\"\"\",\"mouse called \"\"Tiny\"\"\"\r\n"s);
}

TEST(RowWriterTests, TestTwoRows)
{
    std::u8string s;
    auto          iter = std::back_inserter(s);

    std::array<std::u8string, 3> row1{u8"dog"s, u8"cat"s, u8"mouse"s};
    std::array<std::u8string, 3> row2{u8"bear"s, u8"pig"s, u8"moose"s};

    auto writer = m::csv::writer([&](auto spn) { iter = std::ranges::copy(spn, iter).out; });

    writer.write_row(row1);
    writer.write_row(row2);

    EXPECT_EQ(s, u8"dog,cat,mouse\r\nbear,pig,moose\r\n"s);
}

TEST(RowWriterTests, TestQuotedRows)
{
    std::u8string s;
    auto          iter = std::back_inserter(s);

    std::array<std::u8string, 3> row1{u8"dog"s, u8"cat"s, u8"mouse"s};
    std::array<std::u8string, 3> row2{u8"bear"s, u8"a,b,c"s, u8"moose"s};

    auto writer = m::csv::writer([&](auto spn) { iter = std::ranges::copy(spn, iter).out; });

    writer.write_row(row1);
    writer.write_row(row2);

    EXPECT_EQ(s, u8"dog,cat,mouse\r\nbear,\"a,b,c\",moose\r\n"s);
}
