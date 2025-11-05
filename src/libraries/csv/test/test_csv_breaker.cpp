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

#include <m/csv/breaker.h>
#include <m/string_buffer/string_buffer.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(CsvBreakerTests, TestBreakingOneRow)
{
    constexpr auto csv_data = u8"Name,Age,Location\r\nJohn,30,USA\r\nJane,25,UK"sv;

    m::csv::breaker::span_type const csv_span{csv_data.data(), csv_data.size()};

    m::csv::breaker field_breaker;

    std::array<m::u8string_buffer, 3> fields;
    std::size_t                       field_index{};
    std::size_t                       row_index{};

    std::array<std::array<std::u8string_view, 3>, 3> expected_rows{{
        {u8"Name"sv, u8"Age"sv, u8"Location"sv},
        {u8"John"sv, u8"30"sv, u8"USA"sv},
        {u8"Jane"sv, u8"25"sv, u8"UK"sv},
    }};

    field_breaker.find_breaks(csv_span, [&](auto reason, auto field_span) {
        M_INTERNAL_ERROR_CHECK(reason == m::csv::breaker::break_reason::row ||
                                reason == m::csv::breaker::break_reason::field);
        fields[field_index].assign(field_span);
        EXPECT_EQ(std::u8string_view(fields[field_index].c_str()),
                    expected_rows[row_index][field_index]);
        field_index++;
        if (reason == m::csv::breaker::break_reason::row)
        {
            //
            // End of row
            //
            field_index = 0;
            row_index++;
        }
        });
}

