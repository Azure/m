// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#include <m/csv/field_quoter.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(FieldQuoterTests, SimpleString)
{
    std::wstring s;
    auto         iter = std::back_inserter(s);

    auto q = m::csv::field_quoter(iter);

    q.enquote(L"hello"sv);

    EXPECT_EQ(s, L"hello"s);
}

TEST(FieldQuoterTests, SimpleString2)
{
    std::wstring s;
    auto         iter = std::back_inserter(s);

    auto q = m::csv::field_quoter(iter);

    q.enquote(L"hello \"Mike\" if that's actually your name"sv);

    EXPECT_EQ(s, L"\"hello \"\"Mike\"\" if that's actually your name\""s);
}

TEST(FieldQuoterTests, TestCarriageReturn)
{
    std::wstring s;
    auto         iter = std::back_inserter(s);

    auto q = m::csv::field_quoter(iter);

    q.enquote(L"abc\r123"sv);

    // Carriage returns and line feeds simply have double quotes put around the fields
    EXPECT_EQ(s, L"\"abc\r123\""s);
}

TEST(FieldQuoterTests, TestLineFeed)
{
    std::wstring s;
    auto         iter = std::back_inserter(s);

    auto q = m::csv::field_quoter(iter);

    q.enquote(L"abc\n123"sv);

    // Carriage returns and line feeds simply have double quotes put around the fields
    EXPECT_EQ(s, L"\"abc\n123\""s);
}

TEST(FieldQuoterTests, TestCRLF)
{
    std::wstring s;
    auto         iter = std::back_inserter(s);

    auto q = m::csv::field_quoter(iter);

    q.enquote(L"abc\r\n123"sv);

    // Carriage returns and line feeds simply have double quotes put around the fields
    EXPECT_EQ(s, L"\"abc\r\n123\""s);
}

TEST(FieldQuoterTests, TestBrace)
{
    std::wstring s;
    auto         iter = std::back_inserter(s);

    auto q = m::csv::field_quoter(iter);

    q.enquote(L"abc{123"sv);

    //
    // Open brace is mapped to {U+007b} so that { can be fairly safely treated
    // as the trigger for escaping on parsing. The encoder couples escaping with
    // quoting so if it has an open brace, it also gets quoted.
    //
    EXPECT_EQ(s, L"\"abc{U+007b}123\""s);
}

TEST(FieldQuoterTests, TestStartingWithQuotedString)
{
    std::wstring s;
    auto         iter = std::back_inserter(s);

    auto q = m::csv::field_quoter(iter);

    q.enquote(L"\"What\"happens"sv);

    //
    // Expected: Quotes around whole string. Quotes around "What" are doubled.
    //
    EXPECT_EQ(s, L"\"\"\"What\"\"happens\""s);
}

TEST(FieldQuoterTests, TestEndingWithQuotedString)
{
    std::wstring s;
    auto         iter = std::back_inserter(s);

    auto q = m::csv::field_quoter(iter);

    q.enquote(L"What\"happens\""sv);

    //
    // Expected: Quotes around whole string. Quotes around "happens" are doubled.
    //
    EXPECT_EQ(s, L"\"What\"\"happens\"\"\""s);
}

TEST(FieldQuoterTests, TestBEL)
{
    std::wstring s;
    auto         iter = std::back_inserter(s);

    auto q = m::csv::field_quoter(iter);

    q.enquote(L"abc\007123"sv);

    EXPECT_EQ(s, L"\"abc{U+0007}123\""s);
}

//
// It would seem like including non-ASCII strings like in the extended ISO
// Latin-1 would be easy but it's not clear it would have great value.
//
// Perhaps the encoder should be extended to pass more values through without
// escaping but that's more of a spec change for the future. Right now
// it appears to behave.
//
