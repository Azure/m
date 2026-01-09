// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <array>
#include <iterator>
#include <string>
#include <string_view>

#include <m/cp_acp/convert.h>
#include <m/cp_acp/cp_acp.h>
#include <m/strings/convert.h>
#include <m/windows_strings/convert.h>

#include <Windows.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

//
// Since CP_ACP is not a universal constant code page, we can't
// actually test any non-ASCII mappings.
//
// There are hopefully sufficient tests of the underlying multibyte
// API tests for anything interesting around encodings. Instead we
// will verify basic round tripping.
//

struct cp_acp_test_datum
{
    char const*      sptr;
    std::string      str;
    std::string_view sview;

    wchar_t const*    wsptr;
    std::wstring      wstr;
    std::wstring_view wsview;

    char8_t const*     u8sptr;
    std::u8string      u8str;
    std::u8string_view u8sview;

    char16_t const*     u16sptr;
    std::u16string      u16str;
    std::u16string_view u16sview;

    char32_t const*     u32sptr;
    std::u32string      u32str;
    std::u32string_view u32sview;

    template <typename TChar>
    std::basic_string<TChar> const&
    get_string() const
    {
        if constexpr (std::same_as<TChar, char>)
        {
            return str;
        }
        else if constexpr (std::same_as<TChar, wchar_t>)
        {
            return wstr;
        }
        else if constexpr (std::same_as<TChar, char8_t>)
        {
            return u8str;
        }
        else if constexpr (std::same_as<TChar, char16_t>)
        {
            return u16str;
        }
        else if constexpr (std::same_as<TChar, char32_t>)
        {
            return u32str;
        }
        else
        {
            M_UNREACHABLE_CODE();
        }
    }
};

#define DEFINE_TEST_DATUM(identifier, value)                                                       \
    auto              sp_##identifier   = value;                                                   \
    auto              s_##identifier    = value##s;                                                \
    auto              sv_##identifier   = value##sv;                                               \
    auto              wsp_##identifier  = L##value;                                                \
    auto              ws_##identifier   = L##value##s;                                             \
    auto              wsv_##identifier  = L##value##sv;                                            \
    auto              u8sp_##identifier = u8##value;                                               \
    auto              u8s_##identifier  = u8##value##s;                                            \
    auto              u8sv_##identifier = u8##value##sv;                                           \
    auto              usp_##identifier  = u##value;                                                \
    auto              us_##identifier   = u##value##s;                                             \
    auto              usv_##identifier  = u##value##sv;                                            \
    auto              Usp_##identifier  = U##value;                                                \
    auto              Us_##identifier   = U##value##s;                                             \
    auto              Usv_##identifier  = U##value##sv;                                            \
    cp_acp_test_datum identifier##_datum                                                           \
    {                                                                                              \
        .sptr = sp_##identifier, .str = s_##identifier, .sview = sv_##identifier,                  \
        .wsptr = wsp_##identifier, .wstr = ws_##identifier, .wsview = wsv_##identifier,            \
        .u8sptr = u8sp_##identifier, .u8str = u8s_##identifier, .u8sview = u8sv_##identifier,      \
        .u16sptr = usp_##identifier, .u16str = us_##identifier, .u16sview = usv_##identifier,      \
        .u32sptr = Usp_##identifier, .u32str = Us_##identifier, .u32sview = Usv_##identifier       \
    }

DEFINE_TEST_DATUM(t1, "This is test data");
DEFINE_TEST_DATUM(
    t2,
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz");

template <typename TChar>
void
exercise_to_basic_string(cp_acp_test_datum const& d)
{
    auto v1 = m::acp_to_basic_string<TChar>(d.sptr);
    auto v2 = m::acp_to_basic_string<TChar>(d.str);
    auto v3 = m::acp_to_basic_string<TChar>(d.sview);

    EXPECT_EQ(v1, d.get_string<TChar>());
    EXPECT_EQ(v2, d.get_string<TChar>());
    EXPECT_EQ(v3, d.get_string<TChar>());

    std::basic_string<TChar> v4;
    std::basic_string<TChar> v5;
    std::basic_string<TChar> v6;

    m::acp_to_basic_string(d.sptr, v4);
    m::acp_to_basic_string(d.str, v5);
    m::acp_to_basic_string(d.sview, v6);

    EXPECT_EQ(v4, d.get_string<TChar>());
    EXPECT_EQ(v5, d.get_string<TChar>());
    EXPECT_EQ(v6, d.get_string<TChar>());
}

void
exercise_to_wstring(cp_acp_test_datum const& d)
{
    auto v1 = m::acp_to_wstring(d.sptr);
    auto v2 = m::acp_to_wstring(d.str);
    auto v3 = m::acp_to_wstring(d.sview);

    EXPECT_EQ(v1, d.wstr);
    EXPECT_EQ(v2, d.wstr);
    EXPECT_EQ(v3, d.wstr);

    std::wstring v4;
    std::wstring v5;
    std::wstring v6;

    m::acp_to_wstring(d.sptr, v4);
    m::acp_to_wstring(d.str, v5);
    m::acp_to_wstring(d.sview, v6);

    EXPECT_EQ(v4, d.wstr);
    EXPECT_EQ(v5, d.wstr);
    EXPECT_EQ(v6, d.wstr);
}

void
exercise_to_string(cp_acp_test_datum const& d)
{
    auto v1 = m::acp_to_string(d.sptr);
    auto v2 = m::acp_to_string(d.str);
    auto v3 = m::acp_to_string(d.sview);

    EXPECT_EQ(v1, d.str);
    EXPECT_EQ(v2, d.str);
    EXPECT_EQ(v3, d.str);

    std::string v4;
    std::string v5;
    std::string v6;

    m::acp_to_string(d.sptr, v4);
    m::acp_to_string(d.str, v5);
    m::acp_to_string(d.sview, v6);

    EXPECT_EQ(v4, d.str);
    EXPECT_EQ(v5, d.str);
    EXPECT_EQ(v6, d.str);
}

void
exercise_to_u8string(cp_acp_test_datum const& d)
{
    auto v1 = m::acp_to_u8string(d.sptr);
    auto v2 = m::acp_to_u8string(d.str);
    auto v3 = m::acp_to_u8string(d.sview);

    EXPECT_EQ(v1, d.u8str);
    EXPECT_EQ(v2, d.u8str);
    EXPECT_EQ(v3, d.u8str);

    std::u8string v4;
    std::u8string v5;
    std::u8string v6;

    m::acp_to_u8string(d.sptr, v4);
    m::acp_to_u8string(d.str, v5);
    m::acp_to_u8string(d.sview, v6);

    EXPECT_EQ(v4, d.u8str);
    EXPECT_EQ(v5, d.u8str);
    EXPECT_EQ(v6, d.u8str);
}

void
exercise_to_u16string(cp_acp_test_datum const& d)
{
    auto v1 = m::acp_to_u16string(d.sptr);
    auto v2 = m::acp_to_u16string(d.str);
    auto v3 = m::acp_to_u16string(d.sview);

    EXPECT_EQ(v1, d.u16str);
    EXPECT_EQ(v2, d.u16str);
    EXPECT_EQ(v3, d.u16str);

    std::u16string v4;
    std::u16string v5;
    std::u16string v6;

    m::acp_to_u16string(d.sptr, v4);
    m::acp_to_u16string(d.str, v5);
    m::acp_to_u16string(d.sview, v6);

    EXPECT_EQ(v4, d.u16str);
    EXPECT_EQ(v5, d.u16str);
    EXPECT_EQ(v6, d.u16str);
}

void
exercise_to_u32string(cp_acp_test_datum const& d)
{
    auto v1 = m::acp_to_u32string(d.sptr);
    auto v2 = m::acp_to_u32string(d.str);
    auto v3 = m::acp_to_u32string(d.sview);

    EXPECT_EQ(v1, d.u32str);
    EXPECT_EQ(v2, d.u32str);
    EXPECT_EQ(v3, d.u32str);

    std::u32string v4;
    std::u32string v5;
    std::u32string v6;

    m::acp_to_u32string(d.sptr, v4);
    m::acp_to_u32string(d.str, v5);
    m::acp_to_u32string(d.sview, v6);

    EXPECT_EQ(v4, d.u32str);
    EXPECT_EQ(v5, d.u32str);
    EXPECT_EQ(v6, d.u32str);
}

void
exercise_to_acp(cp_acp_test_datum const& d)
{
    auto v1 = m::to_acp_string(d.sptr);
    EXPECT_EQ(v1, d.str);
    auto v2 = m::to_acp_string(d.str);
    EXPECT_EQ(v2, d.str);
    auto v3 = m::to_acp_string(d.sview);
    EXPECT_EQ(v3, d.str);
    auto v4 = m::to_acp_string(d.wsptr);
    EXPECT_EQ(v4, d.str);
    auto v5 = m::to_acp_string(d.wstr);
    EXPECT_EQ(v5, d.str);
    auto v6 = m::to_acp_string(d.wsview);
    EXPECT_EQ(v6, d.str);
    auto v7 = m::to_acp_string(d.u8sptr);
    EXPECT_EQ(v7, d.str);
    auto v8 = m::to_acp_string(d.u8str);
    EXPECT_EQ(v8, d.str);
    auto v9 = m::to_acp_string(d.u8sview);
    EXPECT_EQ(v9, d.str);
    auto v10 = m::to_acp_string(d.u16sptr);
    EXPECT_EQ(v10, d.str);
    auto v11 = m::to_acp_string(d.u16str);
    EXPECT_EQ(v11, d.str);
    auto v12 = m::to_acp_string(d.u16sview);
    EXPECT_EQ(v12, d.str);
    auto v13 = m::to_acp_string(d.u32sptr);
    EXPECT_EQ(v13, d.str);
    auto v14 = m::to_acp_string(d.u32str);
    EXPECT_EQ(v14, d.str);
    auto v15 = m::to_acp_string(d.u32sview);
    EXPECT_EQ(v15, d.str);
}

TEST(AcpAPIs, DoToAcpAcrossData)
{
    exercise_to_acp(t1_datum);
    exercise_to_acp(t2_datum);
}

TEST(AcpAPIs, acp_to_wstring)
{
    exercise_to_wstring(t1_datum);
    exercise_to_wstring(t2_datum);

    exercise_to_basic_string<wchar_t>(t1_datum);
    exercise_to_basic_string<wchar_t>(t2_datum);

    auto s = m::acp_to_wstring(s_t1);
    EXPECT_EQ(s, ws_t1);

    auto s2 = m::acp_to_wstring(sv_t1);
    EXPECT_EQ(s2, ws_t1);

    auto s3 = m::to_wstring(s_t1.c_str());
}

TEST(AcpAPIs, acp_to_u16string)
{
    exercise_to_u16string(t1_datum);
    exercise_to_u16string(t2_datum);

    exercise_to_basic_string<char16_t>(t1_datum);
    exercise_to_basic_string<char16_t>(t2_datum);

    auto s = m::acp_to_u16string(s_t1);
    EXPECT_EQ(s, usv_t1);

    auto s2 = m::acp_to_u16string(sv_t1);
    EXPECT_EQ(s2, usv_t1);

    auto s3 = m::to_u16string(s_t1.c_str());
}

TEST(AcpAPIs, acp_to_u8string)
{
    exercise_to_u8string(t1_datum);
    exercise_to_u8string(t2_datum);

    exercise_to_basic_string<char8_t>(t1_datum);
    exercise_to_basic_string<char8_t>(t2_datum);

    auto s = m::to_u8string(s_t1.c_str());
    //
}

TEST(AcpAPIs, acp_to_u32string)
{
    exercise_to_u32string(t1_datum);
    exercise_to_u32string(t2_datum);

    exercise_to_basic_string<char32_t>(t1_datum);
    exercise_to_basic_string<char32_t>(t2_datum);

    auto s = m::to_u32string(s_t1.c_str());
    //
}

TEST(AcpAPIs, wstring_2_acp)
{
    auto s1 = m::to_acp_string(ws_t1);
    EXPECT_EQ(s1, s_t1);

    auto s2 = m::to_acp_string(wsv_t1);
    EXPECT_EQ(s2, s_t1);
}

TEST(AcpAPIs, u16string_2_acp)
{
    auto s1 = m::to_acp_string(usv_t1);
    EXPECT_EQ(s1, s_t1);
}
