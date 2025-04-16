// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <array>
#include <iterator>
#include <string>
#include <string_view>

#include <m/multi_byte/multi_byte.h>
#include <m/utility/make_span.h>

#include <Windows.h>

#include "multi_byte_test_data.h"

using namespace std::string_literals;
using namespace std::string_view_literals;

struct test_data
{
    std::string m_astring;
    std::wstring m_wstring;

    std::string_view m_asv;
    std::wstring_view m_wsv;
};

test_data data1{"abc123"s, L"abc123"s, "abc123"sv, L"abc123"sv};

void
test_cvt_acp_iter_to_wstring(test_data const& data)
{
    std::wstring out;

    m::multi_byte::acp_to_utf16(data.m_asv.begin(), data.m_asv.end(), out);

    EXPECT_EQ(out, data.m_wstring);

    out.clear();

    m::multi_byte::acp_to_utf16(data.m_asv, out);

    EXPECT_EQ(out, data.m_wstring);

    out.clear();

    m::multi_byte::acp_to_utf16(data.m_astring, out);

    EXPECT_EQ(out, data.m_wstring);
    out.clear();
}

void
test_multibyte_length(mb_test_data const& data)
{
    auto const length = m::multi_byte::multi_byte_to_utf16_length(data.m_cp, data.m_view);
    EXPECT_EQ(length, data.m_wview.size());
}

// Only call this with test data that maps to output of less than 128 characters
void
test_multibyte_to_span(mb_test_data const& data)
{
    std::array<wchar_t, 128> buffer;

    // If this hits, the data isn't going to fit so the test isn't going to work.
    EXPECT_LT(data.m_wview.size(), buffer.size());

    auto span = m::make_span(buffer);

    m::multi_byte::multi_byte_to_utf16(data.m_cp, data.m_view, span);

    EXPECT_EQ(span.size(), data.m_wview.size());
    EXPECT_EQ(data.m_wview.compare(std::wstring_view(span.data(), span.size())), 0);
}

void
test_simple_multi_byte_api(mb_test_data const& data)
{
    auto s1 = m::to_string(data.m_cp, data.m_u16view);

    EXPECT_EQ(s1, data.m_view);

    auto s2 = m::to_string(data.m_cp, data.m_wview);

    EXPECT_EQ(s2, data.m_view);

    auto s3 = m::to_wstring(data.m_cp, data.m_view);

    EXPECT_EQ(s3, data.m_wview);

    auto s4 = m::to_u16string(data.m_cp, data.m_view);

    EXPECT_EQ(s4, data.m_u16view);
}

void
test_multibyte_to_outiter(mb_test_data const& data)
{
    std::wstring buffer;

    auto outit = std::back_inserter(buffer);

    m::multi_byte::multi_byte_to_utf16(data.m_cp, data.m_view, outit);

    EXPECT_EQ(buffer.size(), data.m_wview.size());
    EXPECT_EQ(data.m_wview.compare(std::wstring_view(buffer)), 0);
}

TEST(ValidateACP2UTF16, CvtAcpIterTo_std_wstring) { test_cvt_acp_iter_to_wstring(data1); }

TEST(MB_2_Utf16, Length_cp950_T1) { test_multibyte_length(mb_cp950_t1); }

TEST(MB_2_Utf16, Length_cp950_T2) { test_multibyte_length(mb_cp950_t2); }
TEST(MB_2_Utf16, Length_cp950_T3) { test_multibyte_length(mb_cp950_t3); }
TEST(MB_2_Utf16, Length_cp950_T5) { test_multibyte_length(mb_cp950_t5); }
TEST(MB_2_Utf16, Length_cp950_T6) { test_multibyte_length(mb_cp950_t6); }
TEST(MB_2_Utf16, Length_cp950_T7) { test_multibyte_length(mb_cp950_t7); }

TEST(MB_2_Utf16, IntoSpan_cp950_T1) { test_multibyte_to_span(mb_cp950_t1); }
TEST(MB_2_Utf16, IntoSpan_cp950_T2) { test_multibyte_to_span(mb_cp950_t2); }
TEST(MB_2_Utf16, IntoSpan_cp950_T3) { test_multibyte_to_span(mb_cp950_t3); }
TEST(MB_2_Utf16, IntoSpan_cp950_T4) { test_multibyte_to_span(mb_cp950_t4); }
// TEST(MB_2_Utf16, IntoSpan_cp950_T5) { test_multibyte_to_span(mb_cp950_t5); }

TEST(MB_2_Utf16, IntoOutIter_cp950_T1) { test_multibyte_to_outiter(mb_cp950_t1); }
TEST(MB_2_Utf16, IntoOutIter_cp950_T2) { test_multibyte_to_outiter(mb_cp950_t2); }
TEST(MB_2_Utf16, IntoOutIter_cp950_T3) { test_multibyte_to_outiter(mb_cp950_t3); }
TEST(MB_2_Utf16, IntoOutIter_cp950_T4) { test_multibyte_to_outiter(mb_cp950_t4); }
TEST(MB_2_Utf16, IntoOutIter_cp950_T5) { test_multibyte_to_outiter(mb_cp950_t5); }
TEST(MB_2_Utf16, IntoOutIter_cp950_T6) { test_multibyte_to_outiter(mb_cp950_t6); }
TEST(MB_2_Utf16, IntoOutIter_cp950_T7) { test_multibyte_to_outiter(mb_cp950_t7); }

TEST(MB_2_Utf16, SimpleApis_cp950_T1) { test_simple_multi_byte_api(mb_cp950_t1); }
TEST(MB_2_Utf16, SimpleApis_cp950_T2) { test_simple_multi_byte_api(mb_cp950_t2); }
TEST(MB_2_Utf16, SimpleApis_cp950_T3) { test_simple_multi_byte_api(mb_cp950_t3); }
TEST(MB_2_Utf16, SimpleApis_cp950_T4) { test_simple_multi_byte_api(mb_cp950_t4); }
TEST(MB_2_Utf16, SimpleApis_cp950_T5) { test_simple_multi_byte_api(mb_cp950_t5); }
TEST(MB_2_Utf16, SimpleApis_cp950_T6) { test_simple_multi_byte_api(mb_cp950_t6); }
TEST(MB_2_Utf16, SimpleApis_cp950_T7) { test_simple_multi_byte_api(mb_cp950_t7); }


