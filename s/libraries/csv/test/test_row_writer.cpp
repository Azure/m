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
    std::wstring s;
    auto         iter = std::back_inserter(s);

    std::array<std::wstring, 3> strings{L"dog"s, L"cat"s, L"mouse"s};

    auto writer = m::csv::writer(iter);

    writer.write_row(strings);

    EXPECT_EQ(s, L"dog,cat,mouse"s);
}

TEST(RowWriterTests, TestBlankColumn)
{
    std::wstring s;
    auto         iter = std::back_inserter(s);

    std::array<std::wstring, 3> strings{L"dog"s, L""s, L"mouse"s};

    auto writer = m::csv::writer(iter);

    writer.write_row(strings);

    EXPECT_EQ(s, L"dog,,mouse"s);
}

TEST(RowWriterTests, TestColumnWithSpace)
{
    std::wstring s;
    auto         iter = std::back_inserter(s);

    std::array<std::wstring, 3> strings{L"dog"s, L"cat"s, L"mouse with cheese"s};

    auto writer = m::csv::writer(iter);

    writer.write_row(strings);

    EXPECT_EQ(s, L"dog,cat,mouse with cheese"s);
}

TEST(RowWriterTests, TestABunchOfBlankColumns)
{
    std::wstring s;
    auto         iter = std::back_inserter(s);

    std::array<std::wstring, 5> strings{L""s, L""s, L""s, L""s, L""s};

    auto writer = m::csv::writer(iter);

    writer.write_row(strings);

    EXPECT_EQ(s, L",,,,"s);
}

TEST(RowWriterTests, TestThreeAnimalsWithQuotes)
{
    std::wstring s;
    auto         iter = std::back_inserter(s);

    std::array<std::wstring, 3> strings{L"dog called \"Fido\""s, L"cat called \"Felix\""s, L"mouse called \"Tiny\""s};

    auto writer = m::csv::writer(iter);

    writer.write_row(strings);

    EXPECT_EQ(s, L"\"dog called \"\"Fido\"\"\",\"cat called \"\"Felix\"\"\",\"mouse called \"\"Tiny\"\"\""s);
}

TEST(RowWriterTests, TestTwoRows)
{
    std::wstring s;
    auto         iter = std::back_inserter(s);

    std::array<std::wstring, 3> row1{L"dog"s, L"cat"s, L"mouse"s};
    std::array<std::wstring, 3> row2{L"bear"s, L"pig"s, L"moose"s};

    auto writer = m::csv::writer(iter);

    writer.write_row(row1);
    writer.write_row(row2);
    writer.write_end();

    EXPECT_EQ(s, L"dog,cat,mouse\r\nbear,pig,moose\r\n"s);
}

TEST(RowWriterTests, TestQuotedRows)
{
    std::wstring s;
    auto         iter = std::back_inserter(s);

    std::array<std::wstring, 3> row1{L"dog"s, L"cat"s, L"mouse"s};
    std::array<std::wstring, 3> row2{L"bear"s, L"a,b,c"s, L"moose"s};

    auto writer = m::csv::writer(iter);

    writer.write_row(row1);
    writer.write_row(row2);
    writer.write_end();

    EXPECT_EQ(s, L"dog,cat,mouse\r\nbear,\"a,b,c\",moose\r\n"s);
}
