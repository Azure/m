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

using namespace std::string_literals;
using namespace std::string_view_literals;

//
// It was somewhat difficult to find examples of "common" code pages
// which would elicit failures from MultiByteToWideChar(). The code
// pages would have to be common for the unit tests to be checked in
// and universally be applied and useful.
//
// Code page 1252 is pretty standard on Windows and is based on an old
// attempted copy of ISO Latin-1 (ISO-8859-1). Latin-1 of course evolved
// after 1252, so they're pretty different at this point. Check a reference
// for more history. But 1252 is present on essentially all systems where
// it wasn't removed for extreme space savings measures (think IoT machines).
//
// However, 1252 also doesn't have any double/multi byte sequences,
// and it doesn't have any unmapped characters, so it's out of the
// running. Searching around a bit, the best I could come up with was
// code page 950 ("Big5"). It appears to be on a majority of machines, not
// just because of the volume of Chinese speakers in the world.
//
// Another class of encodings that will cause these failures is the
// encodings for UTF-8, but if you read the documentation, it's clear that
// the UTF-8 encode/decode go through a different path than the normal NLS
// encodings. Well it's not a given, I haven't looked at the code, but it
// seems like there are more restrictions and there is perhaps reason to
// believe that the UTF-8 encode/decode is more performance sensitive?
//
// In any case, we'll just test both, no harm done.
//
// On terminology, some will argue that strictly speaking, all of UTF-8,
// UTF-16, UTF-32, CP-950, etc etc are "multibyte encodings". I think that
// that definition is somewhat useless, since it only leaves character
// encodings limited to 256 (255?) characters in the running for not
// being multibyte encoded. The believers of this faith will call the
// non-single-byte encodings like CP-950 "dbcs" character sets.
//
// Since moving to UTF encodings I had completely forgotten those terrible
// discussions. I apologize for the weird cross of terminology that
// appears in this file, I will accede to this use here since the
// actual code page used for the invalid "DBCS" data is really irrelevant.
//
// Originally I was convinced that there were invalid CP-1252 byte sequences
// (thanks, AI!) so I called everything "InvalidCp1252Whatever", and then
// changed it to a different code page and another and now I just want a
// single symbolic reference - "Dbcs".
//

constexpr m::multi_byte::code_page dbcs_cp = m::multi_byte::code_page{950};
constexpr m::multi_byte::code_page utf8_cp = m::multi_byte::code_page{CP_UTF8};

//
// "Bad Char Array", "Bad String View", and "Bad String", maximally shortened
//

std::array<char const, 4> const bca{'x', '\x81', '\x20', '\0'};
auto const                      bps = bca.data();
auto const                      bsv = std::string_view(bca.data(), bca.size() - 1);
auto const                      bs  = std::string(bsv);

//
// Similarly, an invalid UTF-8 encoding. Trivially, a lead byte with
// no followers.
//
// NOT typed as char8_t. Will just be passed through as CP_UTF8.
//
std::array<char const, 2> const bu8ca{'\xf0', '\0'};
auto const                      bpu8s = bu8ca.data();
auto const                      bu8sv = std::string_view(bu8ca.data(), bu8ca.size() - 1);
auto const                      bu8s  = std::string(bu8sv);

namespace
{
    void
    try_view_to_tstring(m::multi_byte::code_page cp, std::string_view sv)
    {
        //
        // Code page 950 is alleged to be installed on "virtually all Windows
        // installations" but we do the test so that we don't fail randomly.
        //
        if (::IsValidCodePage(m::to_underlying(cp)))
        {

            //
            // Windows Vista and later, if the code page is valid you don't
            // have to worry if it's also loaded.
            //

            //
            // The UTF-8 code page is also always present since Windows 8 or 10,
            // but has more esoteric requirements and will be tested independently.
            // 950 is a more "normal" DBCS/MBCS encoding and presumably will hit
            // more "normal" code paths and situations.
            //

            std::error_code ec;

            //
            // Naval gazing question: should the string -> string "conversion"
            // detect bad encodings? It doesn't have to do anything so... no?
            //
            // ec.clear();
            // std::string s1;
            // m::view_to_tstring(dbcs_cp, bsv, s1, ec);
            // EXPECT_NE(ec.value(), 0);
            // EXPECT_THROW(m::view_to_tstring(dbcs_cp, bsv, s1), std::system_error);

            ec.clear();
            std::wstring ws1;
            m::view_to_tstring(cp, sv, ws1, ec);
            EXPECT_NE(ec.value(), 0);
            EXPECT_THROW(m::view_to_tstring(cp, sv, ws1), std::system_error);

            ec.clear();
            std::u8string u8s1;
            m::view_to_tstring(cp, sv, u8s1, ec);
            EXPECT_NE(ec.value(), 0);
            EXPECT_THROW(m::view_to_tstring(cp, sv, u8s1), std::system_error);

            ec.clear();
            std::u16string u16s1;
            m::view_to_tstring(cp, sv, u16s1, ec);
            EXPECT_NE(ec.value(), 0);
            EXPECT_THROW(m::view_to_tstring(cp, sv, u16s1), std::system_error);

            ec.clear();
            std::u32string u32s1;
            m::view_to_tstring(cp, sv, u32s1, ec);
            EXPECT_NE(ec.value(), 0);
            EXPECT_THROW(m::view_to_tstring(cp, sv, u32s1), std::system_error);
        }
    }
} // namespace

TEST(VerifyMbErrInvalidChars, InvalidDbcsData_view_to_tstring)
{
    try_view_to_tstring(dbcs_cp, bsv);
}

TEST(VerifyMbErrInvalidChars, InvalidUTF8Data_view_to_tstring)
{
    try_view_to_tstring(utf8_cp, bu8sv);
}

namespace
{
    template <typename TTestChar, typename TStringish>
        requires m::stringish<TStringish, char> && m::character<TTestChar>
    void
    try_to_tstring(m::multi_byte::code_page cp, TStringish&& in)
    {
        std::basic_string<TTestChar> r;
        EXPECT_THROW(m::to_tstring(cp, in, r), std::system_error);
        std::error_code ec;
        m::to_tstring(cp, in, r, ec);
        EXPECT_NE(ec.value(), 0);

        EXPECT_THROW(m::to_tstring<TTestChar>(cp, in), std::system_error);
        ec.clear();
        EXPECT_EQ(ec.value(), 0);
        auto r2 = m::to_tstring<TTestChar>(cp, in, ec);
        EXPECT_NE(ec.value(), 0);
        static_assert(std::same_as<decltype(r2), std::basic_string<TTestChar>>);
    }

    template <typename TTestChar>
        requires m::character<TTestChar>
    void
    try_dbcs_to_tstring()
    {
        try_to_tstring<TTestChar>(dbcs_cp, bps);
        try_to_tstring<TTestChar>(dbcs_cp, bsv);
        try_to_tstring<TTestChar>(dbcs_cp, bs);
    }

    template <typename TTestChar>
        requires m::character<TTestChar>
    void
    try_utf8_to_tstring()
    {
        try_to_tstring<TTestChar>(utf8_cp, bpu8s);
        try_to_tstring<TTestChar>(utf8_cp, bu8sv);
        try_to_tstring<TTestChar>(utf8_cp, bu8s);
    }

} // namespace

TEST(VerifyMbErrInvalidChars, InvalidDbcsData_to_tstring)
{
    // try_dbcs_to_tstring<char>();
    try_dbcs_to_tstring<wchar_t>();
    try_dbcs_to_tstring<char8_t>();
    try_dbcs_to_tstring<char16_t>();
    try_dbcs_to_tstring<char32_t>();
}

TEST(VerifyMbErrInvalidChars, InvalidUTF8Data_to_tstring)
{
    // try_dbcs_to_tstring<char>();
    try_utf8_to_tstring<wchar_t>();
    try_utf8_to_tstring<char8_t>();
    try_utf8_to_tstring<char16_t>();
    try_utf8_to_tstring<char32_t>();
}

//
// Repeat, with optional. Breaking out just so that the tests don't become
// monolithic.
//

namespace
{
    template <typename TTestChar, typename TStringish>
        requires m::stringish<TStringish, char> && m::character<TTestChar>
    void
    try_to_tstring_optional(m::multi_byte::code_page cp, std::optional<TStringish> const& in)
    {
        std::optional<std::basic_string<TTestChar>> r;
        EXPECT_THROW(m::to_tstring(cp, in, r), std::system_error);
        std::error_code ec;
        m::to_tstring(cp, in, r, ec);
        EXPECT_NE(ec.value(), 0);

        EXPECT_THROW(m::to_tstring<TTestChar>(cp, in), std::system_error);
        ec.clear();
        EXPECT_EQ(ec.value(), 0);
        auto r2 = m::to_tstring<TTestChar>(cp, in, ec);
        EXPECT_NE(ec.value(), 0);
        static_assert(std::same_as<decltype(r2), std::optional<std::basic_string<TTestChar>>>);
    }

    template <typename TTestChar>
        requires m::character<TTestChar>
    void
    try_dbcs_to_tstring_optional()
    {
        try_to_tstring_optional<TTestChar>(dbcs_cp, std::optional(bsv));
        try_to_tstring_optional<TTestChar>(dbcs_cp, std::optional(bs));
    }

    template <typename TTestChar>
        requires m::character<TTestChar>
    void
    try_utf8_to_tstring_optional()
    {
        try_to_tstring_optional<TTestChar>(utf8_cp, std::optional(bu8sv));
        try_to_tstring_optional<TTestChar>(utf8_cp, std::optional(bu8s));
    }

} // namespace

TEST(VerifyMbErrInvalidChars, InvalidDbcsData_to_tstring_optional)
{
    // try_dbcs_to_tstring_optional<char>();
    try_dbcs_to_tstring_optional<wchar_t>();
    try_dbcs_to_tstring_optional<char8_t>();
    try_dbcs_to_tstring_optional<char16_t>();
    try_dbcs_to_tstring_optional<char32_t>();
}

TEST(VerifyMbErrInvalidChars, InvalidUTF8Data_to_tstring_optional)
{
    // try_dbcs_to_tstring_optional<char>();
    try_utf8_to_tstring_optional<wchar_t>();
    try_utf8_to_tstring_optional<char8_t>();
    try_utf8_to_tstring_optional<char16_t>();
    try_utf8_to_tstring_optional<char32_t>();
}

namespace
{
    template <typename TStringish>
        requires m::stringish<TStringish, char>
    void
    try_to_string(m::multi_byte::code_page cp, TStringish&& in)
    {
        std::string r;
        EXPECT_THROW(m::to_string(cp, in, r), std::system_error);
        std::error_code ec;
        m::to_string(cp, in, r, ec);
        EXPECT_NE(ec.value(), 0);

        EXPECT_THROW(m::to_string(cp, in), std::system_error);
        ec.clear();
        EXPECT_EQ(ec.value(), 0);
        auto r2 = m::to_string(cp, in, ec);
        EXPECT_NE(ec.value(), 0);
        static_assert(std::same_as<decltype(r2), std::string>);
    }

    template <typename TStringish>
        requires m::stringish<TStringish, char>
    void
    try_to_wstring(m::multi_byte::code_page cp, TStringish&& in)
    {
        std::wstring r;
        EXPECT_THROW(m::to_wstring(cp, in, r), std::system_error);
        std::error_code ec;
        m::to_wstring(cp, in, r, ec);
        EXPECT_NE(ec.value(), 0);

        EXPECT_THROW(m::to_wstring(cp, in), std::system_error);
        ec.clear();
        EXPECT_EQ(ec.value(), 0);
        auto r2 = m::to_wstring(cp, in, ec);
        EXPECT_NE(ec.value(), 0);
        static_assert(std::same_as<decltype(r2), std::wstring>);
    }

    template <typename TStringish>
        requires m::stringish<TStringish, char>
    void
    try_to_u8string(m::multi_byte::code_page cp, TStringish&& in)
    {
        std::u8string r;
        EXPECT_THROW(m::to_u8string(cp, in, r), std::system_error);
        std::error_code ec;
        m::to_u8string(cp, in, r, ec);
        EXPECT_NE(ec.value(), 0);

        EXPECT_THROW(m::to_u8string(cp, in), std::system_error);
        ec.clear();
        EXPECT_EQ(ec.value(), 0);
        auto r2 = m::to_u8string(cp, in, ec);
        EXPECT_NE(ec.value(), 0);
        static_assert(std::same_as<decltype(r2), std::u8string>);
    }

    template <typename TStringish>
        requires m::stringish<TStringish, char>
    void
    try_to_u16string(m::multi_byte::code_page cp, TStringish&& in)
    {
        std::u16string r;
        EXPECT_THROW(m::to_u16string(cp, in, r), std::system_error);
        std::error_code ec;
        m::to_u16string(cp, in, r, ec);
        EXPECT_NE(ec.value(), 0);

        EXPECT_THROW(m::to_u16string(cp, in), std::system_error);
        ec.clear();
        EXPECT_EQ(ec.value(), 0);
        auto r2 = m::to_u16string(cp, in, ec);
        EXPECT_NE(ec.value(), 0);
        static_assert(std::same_as<decltype(r2), std::u16string>);
    }

    template <typename TStringish>
        requires m::stringish<TStringish, char>
    void
    try_to_u32string(m::multi_byte::code_page cp, TStringish&& in)
    {
        std::u32string r;
        EXPECT_THROW(m::to_u32string(cp, in, r), std::system_error);
        std::error_code ec;
        m::to_u32string(cp, in, r, ec);
        EXPECT_NE(ec.value(), 0);

        EXPECT_THROW(m::to_u32string(cp, in), std::system_error);
        ec.clear();
        EXPECT_EQ(ec.value(), 0);
        auto r2 = m::to_u32string(cp, in, ec);
        EXPECT_NE(ec.value(), 0);
        static_assert(std::same_as<decltype(r2), std::u32string>);
    }

    template <typename TStringish>
        requires m::stringish<TStringish, char>
    void
    try_to_string_optional(m::multi_byte::code_page cp, std::optional<TStringish> const& in)
    {
        std::optional<std::string> r;
        EXPECT_THROW(m::to_string(cp, in, r), std::system_error);
        std::error_code ec;
        m::to_string(cp, in, r, ec);
        EXPECT_NE(ec.value(), 0);

        EXPECT_THROW(m::to_string(cp, in), std::system_error);
        ec.clear();
        EXPECT_EQ(ec.value(), 0);
        auto r2 = m::to_string(cp, in, ec);
        EXPECT_NE(ec.value(), 0);
        static_assert(std::same_as<decltype(r2), std::optional<std::string>>);
    }

    template <typename TStringish>
        requires m::stringish<TStringish, char>
    void
    try_to_wstring_optional(m::multi_byte::code_page cp, std::optional<TStringish> const& in)
    {
        std::optional<std::wstring> r;
        EXPECT_THROW(m::to_wstring(cp, in, r), std::system_error);
        std::error_code ec;
        m::to_wstring(cp, in, r, ec);
        EXPECT_NE(ec.value(), 0);

        EXPECT_THROW(m::to_wstring(cp, in), std::system_error);
        ec.clear();
        EXPECT_EQ(ec.value(), 0);
        auto r2 = m::to_wstring(cp, in, ec);
        EXPECT_NE(ec.value(), 0);
        static_assert(std::same_as<decltype(r2), std::optional<std::wstring>>);
    }

    template <typename TStringish>
        requires m::stringish<TStringish, char>
    void
    try_to_u8string_optional(m::multi_byte::code_page cp, std::optional<TStringish> const& in)
    {
        std::optional<std::u8string> r;
        EXPECT_THROW(m::to_u8string(cp, in, r), std::system_error);
        std::error_code ec;
        m::to_u8string(cp, in, r, ec);
        EXPECT_NE(ec.value(), 0);

        EXPECT_THROW(m::to_u8string(cp, in), std::system_error);
        ec.clear();
        EXPECT_EQ(ec.value(), 0);
        auto r2 = m::to_u8string(cp, in, ec);
        EXPECT_NE(ec.value(), 0);
        static_assert(std::same_as<decltype(r2), std::optional<std::u8string>>);
    }

    template <typename TStringish>
        requires m::stringish<TStringish, char>
    void
    try_to_u16string_optional(m::multi_byte::code_page cp, std::optional<TStringish> const& in)
    {
        std::optional<std::u16string> r;
        EXPECT_THROW(m::to_u16string(cp, in, r), std::system_error);
        std::error_code ec;
        m::to_u16string(cp, in, r, ec);
        EXPECT_NE(ec.value(), 0);

        EXPECT_THROW(m::to_u16string(cp, in), std::system_error);
        ec.clear();
        EXPECT_EQ(ec.value(), 0);
        auto r2 = m::to_u16string(cp, in, ec);
        EXPECT_NE(ec.value(), 0);
        static_assert(std::same_as<decltype(r2), std::optional<std::u16string>>);
    }

    template <typename TStringish>
        requires m::stringish<TStringish, char>
    void
    try_to_u32string_optional(m::multi_byte::code_page cp, std::optional<TStringish> const& in)
    {
        std::optional<std::u32string> r;
        EXPECT_THROW(m::to_u32string(cp, in, r), std::system_error);
        std::error_code ec;
        m::to_u32string(cp, in, r, ec);
        EXPECT_NE(ec.value(), 0);

        EXPECT_THROW(m::to_u32string(cp, in), std::system_error);
        ec.clear();
        EXPECT_EQ(ec.value(), 0);
        auto r2 = m::to_u32string(cp, in, ec);
        EXPECT_NE(ec.value(), 0);
        static_assert(std::same_as<decltype(r2), std::optional<std::u32string>>);
    }

    void
    try_dbcs_to_string()
    {
        try_to_string(dbcs_cp, bps);
        try_to_string(dbcs_cp, bsv);
        try_to_string(dbcs_cp, bs);
    }

    void
    try_utf8_to_string()
    {
        try_to_string(utf8_cp, bpu8s);
        try_to_string(utf8_cp, bu8sv);
        try_to_string(utf8_cp, bu8s);
    }

    void
    try_dbcs_to_wstring()
    {
        try_to_wstring(dbcs_cp, bps);
        try_to_wstring(dbcs_cp, bsv);
        try_to_wstring(dbcs_cp, bs);
    }

    void
    try_utf8_to_wstring()
    {
        try_to_wstring(utf8_cp, bpu8s);
        try_to_wstring(utf8_cp, bu8sv);
        try_to_wstring(utf8_cp, bu8s);
    }

    void
    try_dbcs_to_u8string()
    {
        try_to_u8string(dbcs_cp, bps);
        try_to_u8string(dbcs_cp, bsv);
        try_to_u8string(dbcs_cp, bs);
    }

    void
    try_utf8_to_u8string()
    {
        try_to_u8string(utf8_cp, bpu8s);
        try_to_u8string(utf8_cp, bu8sv);
        try_to_u8string(utf8_cp, bu8s);
    }

    void
    try_dbcs_to_u16string()
    {
        try_to_u16string(dbcs_cp, bps);
        try_to_u16string(dbcs_cp, bsv);
        try_to_u16string(dbcs_cp, bs);
    }

    void
    try_utf8_to_u16string()
    {
        try_to_u16string(utf8_cp, bpu8s);
        try_to_u16string(utf8_cp, bu8sv);
        try_to_u16string(utf8_cp, bu8s);
    }

    void
    try_dbcs_to_u32string()
    {
        try_to_u32string(dbcs_cp, bps);
        try_to_u32string(dbcs_cp, bsv);
        try_to_u32string(dbcs_cp, bs);
    }

    void
    try_utf8_to_u32string()
    {
        try_to_u32string(utf8_cp, bpu8s);
        try_to_u32string(utf8_cp, bu8sv);
        try_to_u32string(utf8_cp, bu8s);
    }

#if 0
    void
    try_dbcs_to_string_optional()
    {
        // try_to_string_optional(dbcs_cp, bps);
        try_to_string_optional(dbcs_cp, bsv);
        try_to_string_optional(dbcs_cp, bs);
    }
#endif

    void
    try_utf8_to_string_optional()
    {
        // try_to_string_optional(utf8_cp, bpu8s);
        try_to_string_optional(utf8_cp, std::optional(bu8sv));
        try_to_string_optional(utf8_cp, std::optional(bu8s));
    }

    void
    try_dbcs_to_wstring_optional()
    {
        try_to_wstring_optional(dbcs_cp, std::optional(bps));
        try_to_wstring_optional(dbcs_cp, std::optional(bsv));
        try_to_wstring_optional(dbcs_cp, std::optional(bs));
    }

    void
    try_utf8_to_wstring_optional()
    {
        try_to_wstring_optional(utf8_cp, std::optional(bpu8s));
        try_to_wstring_optional(utf8_cp, std::optional(bu8sv));
        try_to_wstring_optional(utf8_cp, std::optional(bu8s));
    }

    void
    try_dbcs_to_u8string_optional()
    {
        try_to_u8string_optional(dbcs_cp, std::optional(bps));
        try_to_u8string_optional(dbcs_cp, std::optional(bsv));
        try_to_u8string_optional(dbcs_cp, std::optional(bs));
    }

    void
    try_utf8_to_u8string_optional()
    {
        try_to_u8string_optional(utf8_cp, std::optional(bpu8s));
        try_to_u8string_optional(utf8_cp, std::optional(bu8sv));
        try_to_u8string_optional(utf8_cp, std::optional(bu8s));
    }

    void
    try_dbcs_to_u16string_optional()
    {
        try_to_u16string_optional(dbcs_cp, std::optional(bps));
        try_to_u16string_optional(dbcs_cp, std::optional(bsv));
        try_to_u16string_optional(dbcs_cp, std::optional(bs));
    }

    void
    try_utf8_to_u16string_optional()
    {
        try_to_u16string_optional(utf8_cp, std::optional(bpu8s));
        try_to_u16string_optional(utf8_cp, std::optional(bu8sv));
        try_to_u16string_optional(utf8_cp, std::optional(bu8s));
    }

    void
    try_dbcs_to_u32string_optional()
    {
        try_to_u32string_optional(dbcs_cp, std::optional(bps));
        try_to_u32string_optional(dbcs_cp, std::optional(bsv));
        try_to_u32string_optional(dbcs_cp, std::optional(bs));
    }

    void
    try_utf8_to_u32string_optional()
    {
        try_to_u32string_optional(utf8_cp, std::optional(bpu8s));
        try_to_u32string_optional(utf8_cp, std::optional(bu8sv));
        try_to_u32string_optional(utf8_cp, std::optional(bu8s));
    }

} // namespace

TEST(VerifyMbErrInvalidChars, InvalidDbcsData_to_xstring)
{
    // try_dbcs_to_string();
    try_dbcs_to_wstring();
    try_dbcs_to_u8string();
    try_dbcs_to_u16string();
    try_dbcs_to_u32string();
}

TEST(VerifyMbErrInvalidChars, InvalidUTF8Data_to_xstring)
{
    // try_utf8_to_string();
    try_utf8_to_wstring();
    try_utf8_to_u8string();
    try_utf8_to_u16string();
    try_utf8_to_u32string();
}

TEST(VerifyMbErrInvalidChars, InvalidDbcsData_to_xstring_optional)
{
    // try_dbcs_to_string_optional();
    try_dbcs_to_wstring_optional();
    try_dbcs_to_u8string_optional();
    try_dbcs_to_u16string_optional();
    try_dbcs_to_u32string_optional();
}

TEST(VerifyMbErrInvalidChars, InvalidUTF8Data_to_xstring_optional)
{
    // try_utf8_to_string_optional();
    try_utf8_to_wstring_optional();
    try_utf8_to_u8string_optional();
    try_utf8_to_u16string_optional();
    try_utf8_to_u32string_optional();
}
