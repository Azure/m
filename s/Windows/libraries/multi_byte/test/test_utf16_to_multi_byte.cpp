// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <array>
#include <iterator>
#include <string>
#include <string_view>

#include <m/multi_byte/convert.h>
#include <m/utility/make_span.h>

#include <Windows.h>

#include "multi_byte_test_data.h"

using namespace std::string_literals;
using namespace std::string_view_literals;

void
test_utf16_length(mb_test_data const& data)
{
    auto const l1 = m::multi_byte::utf16_to_multi_byte_length(data.m_cp, data.m_wview);
    auto const l2 = m::multi_byte::utf16_to_multi_byte_length(data.m_cp, data.m_u16view);
    EXPECT_EQ(l1, data.m_view.size());
    EXPECT_EQ(l2, data.m_view.size());
}

// Only call this with test data that maps to output of less than 128 characters
void
test_utf16_to_span(mb_test_data const& data)
{
    std::array<char, 128> buffer;

    // If this hits, the data isn't going to fit so the test isn't going to work.
    EXPECT_LT(data.m_view.size(), buffer.size());

    auto span = m::make_span(buffer);

    m::multi_byte::utf16_to_multi_byte(data.m_cp, data.m_wview, span);

    auto view = std::string_view(span.begin(), span.end());

    EXPECT_EQ(view.size(), data.m_view.size());
    EXPECT_EQ(data.m_view.compare(view), 0);


    span = m::make_span(buffer);

    m::multi_byte::utf16_to_multi_byte(data.m_cp, data.m_u16view, span);

    view = std::string_view(span.begin(), span.end());

    EXPECT_EQ(view.size(), data.m_view.size());
    EXPECT_EQ(data.m_view.compare(view), 0);
}

void
test_utf16_to_outiter(mb_test_data const& data)
{
    std::string buffer;

    auto outit1 = std::back_inserter(buffer);

    m::multi_byte::utf16_to_multi_byte(data.m_cp, data.m_wview, outit1);

    auto view = std::string_view(buffer);
    EXPECT_EQ(view.size(), data.m_view.size());
    EXPECT_EQ(data.m_view.compare(view), 0);

    buffer.clear();
    auto outit2 = std::back_inserter(buffer);

    m::multi_byte::utf16_to_multi_byte(data.m_cp, data.m_u16view, outit2);

    view = std::string_view(buffer);
    EXPECT_EQ(view.size(), data.m_view.size());
    EXPECT_EQ(data.m_view.compare(view), 0);

    buffer.clear();
}

void
test_utf16_to_string(mb_test_data const& data)
{
    std::string buffer;

    auto s1 = m::multi_byte::utf16_to_multi_byte(data.m_cp, data.m_wview);
    auto s2 = m::multi_byte::utf16_to_multi_byte(data.m_cp, data.m_u16view);

    EXPECT_EQ(s1, s2);
    EXPECT_EQ(s1, data.m_view);
}

// TEST(ValidateACP2UTF16, CvtAcpIterTo_std_wstring) { test_cvt_acp_iter_to_wstring(data1); }

TEST(UTF16_2_MB, Length_cp950_T1) { test_utf16_length(mb_cp950_t1); }

TEST(UTF16_2_MB, Length_cp950_T2) { test_utf16_length(mb_cp950_t2); }
TEST(UTF16_2_MB, Length_cp950_T3) { test_utf16_length(mb_cp950_t3); }
TEST(UTF16_2_MB, Length_cp950_T5) { test_utf16_length(mb_cp950_t5); }
TEST(UTF16_2_MB, Length_cp950_T6) { test_utf16_length(mb_cp950_t6); }
TEST(UTF16_2_MB, Length_cp950_T7) { test_utf16_length(mb_cp950_t7); }

TEST(UTF16_2_MB, IntoSpan_cp950_T1) { test_utf16_to_span(mb_cp950_t1); }
TEST(UTF16_2_MB, IntoSpan_cp950_T2) { test_utf16_to_span(mb_cp950_t2); }
TEST(UTF16_2_MB, IntoSpan_cp950_T3) { test_utf16_to_span(mb_cp950_t3); }
// TEST(UTF16_2_MB, IntoSpan_cp950_T4) { test_utf16_to_span(mb_cp950_t4); }
// TEST(UTF16_2_MB, IntoSpan_cp950_T5) { test_utf16_to_span(mb_cp950_t5); }

TEST(UTF16_2_MB, IntoOutIter_cp950_T1) { test_utf16_to_outiter(mb_cp950_t1); }
TEST(UTF16_2_MB, IntoOutIter_cp950_T2) { test_utf16_to_outiter(mb_cp950_t2); }
TEST(UTF16_2_MB, IntoOutIter_cp950_T3) { test_utf16_to_outiter(mb_cp950_t3); }
TEST(UTF16_2_MB, IntoOutIter_cp950_T4) { test_utf16_to_outiter(mb_cp950_t4); }
TEST(UTF16_2_MB, IntoOutIter_cp950_T5) { test_utf16_to_outiter(mb_cp950_t5); }
TEST(UTF16_2_MB, IntoOutIter_cp950_T6) { test_utf16_to_outiter(mb_cp950_t6); }
TEST(UTF16_2_MB, IntoOutIter_cp950_T7) { test_utf16_to_outiter(mb_cp950_t7); }
