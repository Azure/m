// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <array>
#include <iterator>
#include <string>
#include <string_view>

#include <m/multi_byte/convert.h>
#include <m/sstring/sstring.h>
#include <m/utility/make_span.h>
#include <m/utility/zstring.h>
#include <m/windows_strings/convert.h>

#include <Windows.h>

#include "multi_byte_test_data.h"

using namespace std::string_literals;
using namespace std::string_view_literals;

//
// test_data_base is broken out from acp_test_data because
// otherwise there is no way to have a nullptr ptr value
// use the default constructor for m_view (as far as
// I have been able to figure out).
//

struct acp_test_data_base
{
    acp_test_data_base(char const* ptr): m_ptr(ptr)
    {
        if (ptr != nullptr)
        {
            m_view = std::string_view(ptr);
        }
    }

    char const*      m_ptr;
    std::string_view m_view{};
};

struct acp_test_data : acp_test_data_base
{
    acp_test_data(char const* ptr):
        acp_test_data_base(ptr),
        m_oview(m_view),
        m_string(m_view),
        m_ostring(m_string),
        m_sstring(m_view),
        m_osstring(m_sstring)
    {}

    std::optional<std::string_view> m_oview{};
    std::optional<std::string_view> m_oview_nv{}; // no value - not initialized
    std::string                     m_string{};
    std::optional<std::string>      m_ostring{};
    std::optional<std::string>      m_ostring_nv{}; // no value - not initialized
    m::sstring                      m_sstring{};
    std::optional<m::sstring>       m_osstring{};
    std::optional<m::sstring>       m_osstring_nv{}; // no value - not initialized
};

namespace data
{
    static inline auto d_null  = acp_test_data(nullptr);
    static inline auto d_short = acp_test_data("hi");
    static inline auto d_medium =
        acp_test_data("Once upon a midnight dreary, as I pondered, weak and weary");

#define LONGISH_TEXT                                                                               \
    "You have the power to strip away many superfluous troubles located wholly in "                \
    "your judgment, and to possess a large room for yourself embracing in thought "                \
    "the whole cosmos, to consider everlasting time, to think of the rapid change "                \
    "in the parts of each thing, of how short it is from birth until dissolution, "                \
    "and how the void before birth and that after dissolution are equally infinite."

    static inline auto d_longish = acp_test_data(LONGISH_TEXT);
    static inline auto d_pretty_long =
        acp_test_data(LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT);

    static inline auto d_getting_long = acp_test_data(
        LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT
            LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT
                LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT
                    LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT);

    static inline auto d_now_thats_long = acp_test_data(
        LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT
            LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT
                LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT
                    LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT
                        LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT
                            LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT
                                LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT
                                    LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT
                                        LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT
                                            LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT
                                                LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT
                                                    LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT
                                                        LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT
                                                            LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT
                                                                LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT
                                                                    LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT
                                                                        LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT
                                                                            LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT
                                                                                LONGISH_TEXT LONGISH_TEXT LONGISH_TEXT
                                                                                    LONGISH_TEXT LONGISH_TEXT
                                                                                        LONGISH_TEXT LONGISH_TEXT
                                                                                            LONGISH_TEXT LONGISH_TEXT
                                                                                                LONGISH_TEXT);

    static inline std::array d_data = {&d_null,
                                       &d_short,
                                       &d_medium,
                                       &d_longish,
                                       &d_pretty_long,
                                       &d_getting_long,
                                       &d_now_thats_long};
} // namespace data

TEST(AcpConversionMatrix, AcpToChar8)
{
    for (auto const& td: data::d_data)
    {
        if (td == nullptr)
            continue;
        //
        //
        //
        auto p_v1 = m::to_u8string(td->m_ptr);

        //
        // These next few things take the values in the test data and round-trip
        // them to the UTF-typed data and back to see if they come back in the
        // same shape they were in originally.
        //
        auto v_v1      = m::to_u8string(td->m_view);
        auto v_v1_1    = m::to_string(v_v1);
        using v_v1_t   = decltype(v_v1);
        using v_v1_1_t = decltype(v_v1_1);
        static_assert(std::is_same_v<v_v1_t, std::u8string>);
        static_assert(std::is_same_v<v_v1_1_t, std::string>);
        EXPECT_EQ(v_v1_1, td->m_view);

        auto v_v2      = m::to_u8string(td->m_oview);
        auto v_v2_1    = m::to_string(v_v2);
        using v_v2_t   = decltype(v_v2);
        using v_v2_1_t = decltype(v_v2_1);
        static_assert(std::is_same_v<v_v2_t, std::optional<std::u8string>>);
        static_assert(std::is_same_v<v_v2_1_t, std::optional<std::string>>);
        EXPECT_EQ(td->m_oview.has_value(), v_v2.has_value());
        EXPECT_EQ(v_v2.has_value(), v_v2_1.has_value());

        if (v_v2_1.has_value())
        {
            EXPECT_EQ(v_v2_1.value(), td->m_oview.value());
        }

        auto v_v3      = m::to_u8string(td->m_oview_nv);
        auto v_v3_1    = m::to_string(v_v3);
        using v_v3_t   = decltype(v_v3);
        using v_v3_1_t = decltype(v_v3_1);
        static_assert(std::is_same_v<v_v3_t, std::optional<std::u8string>>);
        static_assert(std::is_same_v<v_v3_1_t, std::optional<std::string>>);
        EXPECT_FALSE(td->m_oview_nv.has_value());
        EXPECT_FALSE(v_v3.has_value());
        EXPECT_FALSE(v_v3_1.has_value());

        auto s_v1      = m::to_u8string(td->m_string);
        auto s_v1_1    = m::to_string(s_v1);
        using s_v1_t   = decltype(s_v1);
        using s_v1_1_t = decltype(s_v1_1);
        static_assert(std::is_same_v<s_v1_t, std::u8string>);
        static_assert(std::is_same_v<s_v1_1_t, std::string>);
        EXPECT_EQ(s_v1_1, td->m_string);

        auto s_v2      = m::to_u8string(td->m_ostring);
        auto s_v2_1    = m::to_string(s_v2);
        using s_v2_t   = decltype(s_v2);
        using s_v2_1_t = decltype(s_v2_1);
        static_assert(std::is_same_v<s_v2_t, std::optional<std::u8string>>);
        static_assert(std::is_same_v<s_v2_1_t, std::optional<std::string>>);
        EXPECT_EQ(td->m_ostring.has_value(), s_v2.has_value());
        EXPECT_EQ(s_v2.has_value(), s_v2_1.has_value());

        if (s_v2_1.has_value())
        {
            EXPECT_EQ(s_v2_1.value(), td->m_ostring.value());
        }

        auto s_v3      = m::to_u8string(td->m_ostring_nv);
        auto s_v3_1    = m::to_string(s_v3);
        using s_v3_t   = decltype(s_v3);
        using s_v3_1_t = decltype(s_v3_1);
        static_assert(std::is_same_v<s_v3_t, std::optional<std::u8string>>);
        static_assert(std::is_same_v<s_v3_1_t, std::optional<std::string>>);
        EXPECT_FALSE(td->m_ostring_nv.has_value());
        EXPECT_FALSE(s_v3.has_value());
        EXPECT_FALSE(s_v3_1.has_value());

        auto ss_v1      = m::to_u8string(td->m_sstring);
        auto ss_v1_1    = m::to_sstring(ss_v1);
        using ss_v1_t   = decltype(ss_v1);
        using ss_v1_1_t = decltype(ss_v1_1);
        static_assert(std::is_same_v<ss_v1_t, std::u8string>);
        static_assert(std::is_same_v<ss_v1_1_t, m::sstring>);
        EXPECT_EQ(ss_v1_1, td->m_sstring);

        auto ss_v2      = m::to_u8string(td->m_osstring);
        auto ss_v2_1    = m::to_sstring(ss_v2);
        using ss_v2_t   = decltype(ss_v2);
        using ss_v2_1_t = decltype(ss_v2_1);
        static_assert(std::is_same_v<ss_v2_t, std::optional<std::u8string>>);
        static_assert(std::is_same_v<ss_v2_1_t, std::optional<m::sstring>>);
        EXPECT_EQ(td->m_osstring.has_value(), ss_v2.has_value());
        EXPECT_EQ(ss_v2.has_value(), ss_v2_1.has_value());

        if (ss_v2_1.has_value())
        {
            EXPECT_EQ(ss_v2_1.value(), td->m_osstring.value());
        }

        auto ss_v3      = m::to_u8string(td->m_osstring_nv);
        auto ss_v3_1    = m::to_sstring(ss_v3);
        using ss_v3_t   = decltype(ss_v3);
        using ss_v3_1_t = decltype(ss_v3_1);
        static_assert(std::is_same_v<ss_v3_t, std::optional<std::u8string>>);
        static_assert(std::is_same_v<ss_v3_1_t, std::optional<m::sstring>>);
        EXPECT_FALSE(td->m_osstring_nv.has_value());
        EXPECT_FALSE(ss_v3.has_value());
        EXPECT_FALSE(ss_v3_1.has_value());
    }
}

TEST(AcpConversionMatrix, AcpToChar16)
{
    for (auto const& td: data::d_data)
    {
        if (td == nullptr)
            continue;
        //
        //
        //
        auto p_v1 = m::to_u16string(td->m_ptr);

        //
        // These next few things take the values in the test data and round-trip
        // them to the UTF-typed data and back to see if they come back in the
        // same shape they were in originally.
        //
        auto v_v1   = m::to_u16string(td->m_view);
        auto v_v1_1 = m::to_string(v_v1);
        EXPECT_EQ(v_v1_1, td->m_view);

        auto v_v2   = m::to_u16string(td->m_oview);
        auto v_v2_1 = m::to_string(v_v2);
        EXPECT_EQ(v_v2_1, td->m_oview);

        auto v_v3   = m::to_u16string(td->m_oview_nv);
        auto v_v3_1 = m::to_string(v_v3);
        EXPECT_EQ(v_v3_1, td->m_oview_nv);

        auto s_v1   = m::to_u16string(td->m_string);
        auto s_v1_1 = m::to_string(s_v1);
        EXPECT_EQ(s_v1_1, td->m_string);

        auto s_v2   = m::to_u16string(td->m_ostring);
        auto s_v2_1 = m::to_string(s_v2);
        EXPECT_EQ(s_v2_1, td->m_ostring);

        auto s_v3   = m::to_u16string(td->m_ostring_nv);
        auto s_v3_1 = m::to_string(s_v3);
        EXPECT_EQ(s_v3_1, td->m_ostring_nv);

        auto ss_v1   = m::to_u16string(td->m_sstring);
        auto ss_v1_1 = m::to_sstring(ss_v1);
        EXPECT_EQ(ss_v1_1, td->m_sstring);

        auto ss_v2   = m::to_u16string(td->m_osstring);
        auto ss_v2_1 = m::to_sstring(ss_v2);
        EXPECT_EQ(ss_v2_1, td->m_osstring);

        auto ss_v3   = m::to_u16string(td->m_osstring_nv);
        auto ss_v3_1 = m::to_sstring(ss_v3);
        EXPECT_EQ(ss_v3_1, td->m_osstring_nv);
    }
}

TEST(AcpConversionMatrix, AcpToChar32)
{
    for (auto const& td: data::d_data)
    {
        if (td == nullptr)
            continue;
        //
        //
        //
        auto p_v1 = m::to_u32string(td->m_ptr);

        //
        // These next few things take the values in the test data and round-trip
        // them to the UTF-typed data and back to see if they come back in the
        // same shape they were in originally.
        //
        auto v_v1   = m::to_u32string(td->m_view);
        auto v_v1_1 = m::to_string(v_v1);
        EXPECT_EQ(v_v1_1, td->m_view);

        auto v_v2   = m::to_u32string(td->m_oview);
        auto v_v2_1 = m::to_string(v_v2);
        EXPECT_EQ(v_v2_1, td->m_oview);

        auto v_v3   = m::to_u32string(td->m_oview_nv);
        auto v_v3_1 = m::to_string(v_v3);
        EXPECT_EQ(v_v3_1, td->m_oview_nv);

        auto s_v1   = m::to_u32string(td->m_string);
        auto s_v1_1 = m::to_string(s_v1);
        EXPECT_EQ(s_v1_1, td->m_string);

        auto s_v2   = m::to_u32string(td->m_ostring);
        auto s_v2_1 = m::to_string(s_v2);
        EXPECT_EQ(s_v2_1, td->m_ostring);

        auto s_v3   = m::to_u32string(td->m_ostring_nv);
        auto s_v3_1 = m::to_string(s_v3);
        EXPECT_EQ(s_v3_1, td->m_ostring_nv);

        auto ss_v1   = m::to_u32string(td->m_sstring);
        auto ss_v1_1 = m::to_sstring(ss_v1);
        EXPECT_EQ(ss_v1_1, td->m_sstring);

        auto ss_v2   = m::to_u32string(td->m_osstring);
        auto ss_v2_1 = m::to_sstring(ss_v2);
        EXPECT_EQ(ss_v2_1, td->m_osstring);

        auto ss_v3   = m::to_u32string(td->m_osstring_nv);
        auto ss_v3_1 = m::to_sstring(ss_v3);
        EXPECT_EQ(ss_v3_1, td->m_osstring_nv);
    }
}

TEST(AcpConversionMatrix, AcpToWChar)
{
    for (auto const& td: data::d_data)
    {
        if (td == nullptr)
            continue;
        //
        //
        //
        auto p_v1 = m::to_wstring(td->m_ptr);

        //
        // These next few things take the values in the test data and round-trip
        // them to the UTF-typed data and back to see if they come back in the
        // same shape they were in originally.
        //
        auto v_v1   = m::to_wstring(td->m_view);
        auto v_v1_1 = m::to_string(v_v1);
        EXPECT_EQ(v_v1_1, td->m_view);

        auto v_v2   = m::to_wstring(td->m_oview);
        auto v_v2_1 = m::to_string(v_v2);
        EXPECT_EQ(v_v2_1, td->m_oview);

        auto v_v3   = m::to_wstring(td->m_oview_nv);
        auto v_v3_1 = m::to_string(v_v3);
        EXPECT_EQ(v_v3_1, td->m_oview_nv);

        auto s_v1   = m::to_wstring(td->m_string);
        auto s_v1_1 = m::to_string(s_v1);
        EXPECT_EQ(s_v1_1, td->m_string);

        auto s_v2   = m::to_wstring(td->m_ostring);
        auto s_v2_1 = m::to_string(s_v2);
        EXPECT_EQ(s_v2_1, td->m_ostring);

        auto s_v3   = m::to_wstring(td->m_ostring_nv);
        auto s_v3_1 = m::to_string(s_v3);
        EXPECT_EQ(s_v3_1, td->m_ostring_nv);

        auto ss_v1   = m::to_wstring(td->m_sstring);
        auto ss_v1_1 = m::to_sstring(ss_v1);
        EXPECT_EQ(ss_v1_1, td->m_sstring);

        auto ss_v2   = m::to_wstring(td->m_osstring);
        auto ss_v2_1 = m::to_sstring(ss_v2);
        EXPECT_EQ(ss_v2_1, td->m_osstring);

        auto ss_v3   = m::to_wstring(td->m_osstring_nv);
        auto ss_v3_1 = m::to_sstring(ss_v3);
        EXPECT_EQ(ss_v3_1, td->m_osstring_nv);
    }
}

#if 0

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
#endif
