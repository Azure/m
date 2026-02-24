// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>

#include <m/platform_adaptive_strings/convert.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

// ============================================================================
// Helpers
// ============================================================================

// Convert a string_converter call to a recognisable alias so test bodies stay
// concise.
template <typename From, typename To, typename Arg>
static To
cvt(Arg&& arg)
{
    return m::string_converter<std::decay_t<Arg>, To>::make_string(std::forward<Arg>(arg));
}

template <typename From, typename To, typename Arg>
static std::optional<To>
cvt_opt(Arg&& arg)
{
    return m::string_converter<From, To>::make_string(std::optional<From>{std::forward<Arg>(arg)});
}

// ===========================================================================
// char / std::string → std::wstring
// ===========================================================================

TEST(CharToWstring, NullRawPointer)
{
    auto result = m::string_converter<char const*, std::wstring>::make_string(nullptr);
    EXPECT_TRUE(result.empty());
}

TEST(CharToWstring, EmptyRawPointer)
{
    auto result = m::string_converter<char const*, std::wstring>::make_string("");
    EXPECT_TRUE(result.empty());
}

TEST(CharToWstring, SimpleAsciiRawPointer)
{
    auto result = m::string_converter<char const*, std::wstring>::make_string("hello");
    EXPECT_EQ(result, L"hello");
}

TEST(CharToWstring, StringViewEmpty)
{
    auto result = m::string_converter<std::string_view, std::wstring>::make_string(""sv);
    EXPECT_TRUE(result.empty());
}

TEST(CharToWstring, StringViewAscii)
{
    auto result = m::string_converter<std::string_view, std::wstring>::make_string("world"sv);
    EXPECT_EQ(result, L"world");
}

TEST(CharToWstring, StringAscii)
{
    auto result = m::string_converter<std::string, std::wstring>::make_string("hello world"s);
    EXPECT_EQ(result, L"hello world");
}

TEST(CharToWstring, OptionalNullopt)
{
    auto result = m::string_converter<std::string_view, std::wstring>::make_string(
        std::optional<std::string_view>{std::nullopt});
    EXPECT_FALSE(result.has_value());
}

TEST(CharToWstring, OptionalValue)
{
    auto result = m::string_converter<std::string_view, std::wstring>::make_string(
        std::optional<std::string_view>{"hi"sv});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), L"hi");
}

TEST(CharToWstring, OptionalStringNullopt)
{
    auto result = m::string_converter<std::string, std::wstring>::make_string(
        std::optional<std::string>{std::nullopt});
    EXPECT_FALSE(result.has_value());
}

TEST(CharToWstring, OptionalStringValue)
{
    auto result = m::string_converter<std::string, std::wstring>::make_string(
        std::optional<std::string>{"bye"s});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), L"bye");
}

// ===========================================================================
// wchar_t / std::wstring → std::string
// ===========================================================================

TEST(WcharToString, NullRawPointer)
{
    auto result = m::string_converter<wchar_t const*, std::string>::make_string(nullptr);
    EXPECT_TRUE(result.empty());
}

TEST(WcharToString, EmptyRawPointer)
{
    auto result = m::string_converter<wchar_t const*, std::string>::make_string(L"");
    EXPECT_TRUE(result.empty());
}

TEST(WcharToString, SimpleAsciiRawPointer)
{
    auto result = m::string_converter<wchar_t const*, std::string>::make_string(L"hello");
    EXPECT_EQ(result, "hello");
}

TEST(WcharToString, WstringViewAscii)
{
    auto result = m::string_converter<std::wstring_view, std::string>::make_string(L"world"sv);
    EXPECT_EQ(result, "world");
}

TEST(WcharToString, WstringAscii)
{
    auto result = m::string_converter<std::wstring, std::string>::make_string(L"hello world"s);
    EXPECT_EQ(result, "hello world");
}

TEST(WcharToString, OptionalNullopt)
{
    auto result = m::string_converter<std::wstring_view, std::string>::make_string(
        std::optional<std::wstring_view>{std::nullopt});
    EXPECT_FALSE(result.has_value());
}

TEST(WcharToString, OptionalValue)
{
    auto result = m::string_converter<std::wstring_view, std::string>::make_string(
        std::optional<std::wstring_view>{L"hi"sv});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "hi");
}

TEST(WcharToString, OptionalWstringNullopt)
{
    auto result = m::string_converter<std::wstring, std::string>::make_string(
        std::optional<std::wstring>{std::nullopt});
    EXPECT_FALSE(result.has_value());
}

TEST(WcharToString, OptionalWstringValue)
{
    auto result = m::string_converter<std::wstring, std::string>::make_string(
        std::optional<std::wstring>{L"bye"s});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "bye");
}

// Round-trip: ASCII char <-> wchar_t
TEST(CharWcharRoundTrip, Ascii)
{
    std::string const original{"The quick brown fox"};
    auto              wide = m::string_converter<std::string, std::wstring>::make_string(original);
    auto              narrow = m::string_converter<std::wstring, std::string>::make_string(wide);
    EXPECT_EQ(narrow, original);
}

// ===========================================================================
// char8_t / std::u8string → std::wstring
// ===========================================================================

TEST(Char8ToWstring, NullRawPointer)
{
    auto result = m::string_converter<char8_t const*, std::wstring>::make_string(nullptr);
    EXPECT_TRUE(result.empty());
}

TEST(Char8ToWstring, EmptyRawPointer)
{
    auto result = m::string_converter<char8_t const*, std::wstring>::make_string(u8"");
    EXPECT_TRUE(result.empty());
}

TEST(Char8ToWstring, Ascii)
{
    auto result = m::string_converter<char8_t const*, std::wstring>::make_string(u8"hello");
    EXPECT_EQ(result, L"hello");
}

TEST(Char8ToWstring, TwoByteCodepoint)
{
    // U+00E9 é → UTF-8 C3 A9; UTF-16: 0x00E9
    auto result = m::string_converter<std::u8string_view, std::wstring>::make_string(u8"\u00E9"sv);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], L'\u00E9');
}

TEST(Char8ToWstring, ThreeByteCodepoint)
{
    // U+4E2D 中 → UTF-8 E4 B8 AD; UTF-16: 0x4E2D
    auto result = m::string_converter<std::u8string_view, std::wstring>::make_string(u8"\u4E2D"sv);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], L'\u4E2D');
}

TEST(Char8ToWstring, SurrogatePair)
{
    // U+1F600 😀 → UTF-8 F0 9F 98 80
    // On Windows (16-bit wchar_t / UTF-16): surrogate pair D83D DE00
    // On Linux   (32-bit wchar_t / UTF-32): single code point 0x1F600
    auto result =
        m::string_converter<std::u8string_view, std::wstring>::make_string(u8"\U0001F600"sv);
#if WCHAR_MAX == 0xFFFF
    // 16-bit wchar_t: expect UTF-16 surrogate pair
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(static_cast<unsigned>(result[0]), 0xD83Du);
    EXPECT_EQ(static_cast<unsigned>(result[1]), 0xDE00u);
#else
    // 32-bit wchar_t: expect single code point
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(static_cast<unsigned>(result[0]), 0x1F600u);
#endif
}

TEST(Char8ToWstring, U8stringValue)
{
    auto result = m::string_converter<std::u8string, std::wstring>::make_string(u8"test"s);
    EXPECT_EQ(result, L"test");
}

TEST(Char8ToWstring, OptionalNullopt)
{
    auto result = m::string_converter<std::u8string_view, std::wstring>::make_string(
        std::optional<std::u8string_view>{std::nullopt});
    EXPECT_FALSE(result.has_value());
}

TEST(Char8ToWstring, OptionalValue)
{
    auto result = m::string_converter<std::u8string_view, std::wstring>::make_string(
        std::optional<std::u8string_view>{u8"ok"sv});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), L"ok");
}

// ===========================================================================
// wchar_t / std::wstring → std::u8string
// ===========================================================================

TEST(WcharToU8string, NullRawPointer)
{
    auto result = m::string_converter<wchar_t const*, std::u8string>::make_string(nullptr);
    EXPECT_TRUE(result.empty());
}

TEST(WcharToU8string, Ascii)
{
    auto result = m::string_converter<wchar_t const*, std::u8string>::make_string(L"hello");
    EXPECT_EQ(result, u8"hello");
}

TEST(WcharToU8string, TwoByteCodepoint)
{
    auto result = m::string_converter<std::wstring_view, std::u8string>::make_string(L"\u00E9"sv);
    EXPECT_EQ(result, u8"\u00E9");
}

TEST(WcharToU8string, SurrogatePair)
{
    // U+1F600 😀 in UTF-16 = surrogate pair D83D DE00; UTF-8 = F0 9F 98 80
    auto result =
        m::string_converter<std::wstring_view, std::u8string>::make_string(L"\U0001F600"sv);
    EXPECT_EQ(result, u8"\U0001F600");
}

TEST(WcharToU8string, WstringValue)
{
    auto result = m::string_converter<std::wstring, std::u8string>::make_string(L"test"s);
    EXPECT_EQ(result, u8"test");
}

TEST(WcharToU8string, OptionalNullopt)
{
    auto result = m::string_converter<std::wstring_view, std::u8string>::make_string(
        std::optional<std::wstring_view>{std::nullopt});
    EXPECT_FALSE(result.has_value());
}

TEST(WcharToU8string, OptionalValue)
{
    auto result = m::string_converter<std::wstring_view, std::u8string>::make_string(
        std::optional<std::wstring_view>{L"hi"sv});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), u8"hi");
}

// Round-trip: UTF-8 <-> wchar_t (platform-independent Unicode values)
TEST(U8WcharRoundTrip, Ascii)
{
    std::u8string const original{u8"abcdefghij"};
    auto                wide = m::string_converter<std::u8string_view, std::wstring>::make_string(
        std::u8string_view(original));
    auto back = m::string_converter<std::wstring, std::u8string>::make_string(wide);
    EXPECT_EQ(back, original);
}

TEST(U8WcharRoundTrip, MultibyteCodepoints)
{
    std::u8string const original{u8"\u00E9\u4E2D\U0001F600"};
    auto                wide = m::string_converter<std::u8string_view, std::wstring>::make_string(
        std::u8string_view(original));
    auto back = m::string_converter<std::wstring, std::u8string>::make_string(wide);
    EXPECT_EQ(back, original);
}

// ===========================================================================
// char16_t / std::u16string → std::wstring  (and back)
// ===========================================================================

TEST(Char16ToWstring, NullRawPointer)
{
    auto result = m::string_converter<char16_t const*, std::wstring>::make_string(nullptr);
    EXPECT_TRUE(result.empty());
}

TEST(Char16ToWstring, Ascii)
{
    auto result = m::string_converter<char16_t const*, std::wstring>::make_string(u"hello");
    EXPECT_EQ(result, L"hello");
}

TEST(Char16ToWstring, TwoByteCodepoint)
{
    auto result = m::string_converter<std::u16string_view, std::wstring>::make_string(u"\u00E9"sv);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], L'\u00E9');
}

TEST(Char16ToWstring, U16stringValue)
{
    auto result = m::string_converter<std::u16string, std::wstring>::make_string(u"test"s);
    EXPECT_EQ(result, L"test");
}

TEST(Char16ToWstring, OptionalNullopt)
{
    auto result = m::string_converter<std::u16string_view, std::wstring>::make_string(
        std::optional<std::u16string_view>{std::nullopt});
    EXPECT_FALSE(result.has_value());
}

TEST(WcharToU16string, NullRawPointer)
{
    auto result = m::string_converter<wchar_t const*, std::u16string>::make_string(nullptr);
    EXPECT_TRUE(result.empty());
}

TEST(WcharToU16string, Ascii)
{
    auto result = m::string_converter<wchar_t const*, std::u16string>::make_string(L"hello");
    EXPECT_EQ(result, u"hello");
}

TEST(WcharToU16string, TwoByteCodepoint)
{
    auto result = m::string_converter<std::wstring_view, std::u16string>::make_string(L"\u00E9"sv);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], u'\u00E9');
}

TEST(WcharToU16string, WstringValue)
{
    auto result = m::string_converter<std::wstring, std::u16string>::make_string(L"test"s);
    EXPECT_EQ(result, u"test");
}

// Round-trip: wchar ↔ char16 (platform-independent)
TEST(WcharU16RoundTrip, Ascii)
{
    std::wstring const original{L"hello world"};
    auto u16  = m::string_converter<std::wstring, std::u16string>::make_string(original);
    auto back = m::string_converter<std::u16string, std::wstring>::make_string(u16);
    EXPECT_EQ(back, original);
}

TEST(WcharU16RoundTrip, MultibyteCodepoints)
{
    std::wstring const original{L"\u00E9\u4E2D\U0001F600"};
    auto u16  = m::string_converter<std::wstring, std::u16string>::make_string(original);
    auto back = m::string_converter<std::u16string, std::wstring>::make_string(u16);
    EXPECT_EQ(back, original);
}

// ===========================================================================
// char16_t / std::u16string → std::string  (and back)
// ===========================================================================

TEST(Char16ToString, NullRawPointer)
{
    auto result = m::string_converter<char16_t const*, std::string>::make_string(nullptr);
    EXPECT_TRUE(result.empty());
}

TEST(Char16ToString, AsciiRawPointer)
{
    auto result = m::string_converter<char16_t const*, std::string>::make_string(u"hello");
    EXPECT_EQ(result, "hello");
}

TEST(Char16ToString, U16stringViewAscii)
{
    auto result = m::string_converter<std::u16string_view, std::string>::make_string(u"world"sv);
    EXPECT_EQ(result, "world");
}

TEST(Char16ToString, U16stringAscii)
{
    auto result = m::string_converter<std::u16string, std::string>::make_string(u"hello world"s);
    EXPECT_EQ(result, "hello world");
}

TEST(Char16ToString, OptionalNullopt)
{
    auto result = m::string_converter<std::u16string_view, std::string>::make_string(
        std::optional<std::u16string_view>{std::nullopt});
    EXPECT_FALSE(result.has_value());
}

TEST(CharToU16string, NullRawPointer)
{
    auto result = m::string_converter<char const*, std::u16string>::make_string(nullptr);
    EXPECT_TRUE(result.empty());
}

TEST(CharToU16string, AsciiRawPointer)
{
    auto result = m::string_converter<char const*, std::u16string>::make_string("hello");
    EXPECT_EQ(result, u"hello");
}

TEST(CharToU16string, StringViewAscii)
{
    auto result = m::string_converter<std::string_view, std::u16string>::make_string("world"sv);
    EXPECT_EQ(result, u"world");
}

TEST(CharToU16string, StringAscii)
{
    auto result = m::string_converter<std::string, std::u16string>::make_string("hello world"s);
    EXPECT_EQ(result, u"hello world");
}

TEST(CharToU16string, OptionalNullopt)
{
    auto result = m::string_converter<std::string_view, std::u16string>::make_string(
        std::optional<std::string_view>{std::nullopt});
    EXPECT_FALSE(result.has_value());
}

// Round-trip: ASCII char <-> char16
TEST(CharU16RoundTrip, Ascii)
{
    std::string const original{"round trip test"};
    auto              u16 = m::string_converter<std::string, std::u16string>::make_string(original);
    auto              back = m::string_converter<std::u16string, std::string>::make_string(u16);
    EXPECT_EQ(back, original);
}

// ===========================================================================
// char32_t / std::u32string → std::wstring  (and back)
// ===========================================================================

TEST(Char32ToWstring, NullRawPointer)
{
    auto result = m::string_converter<char32_t const*, std::wstring>::make_string(nullptr);
    EXPECT_TRUE(result.empty());
}

TEST(Char32ToWstring, Ascii)
{
    auto result = m::string_converter<char32_t const*, std::wstring>::make_string(U"hello");
    EXPECT_EQ(result, L"hello");
}

TEST(Char32ToWstring, TwoByteCodepoint)
{
    auto result = m::string_converter<std::u32string_view, std::wstring>::make_string(U"\u00E9"sv);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], L'\u00E9');
}

TEST(Char32ToWstring, SurrogatePair)
{
    // U+1F600 → UTF-16 surrogate pair (2 wchar_t on Windows, 1 wchar_t on Linux)
    auto result =
        m::string_converter<std::u32string_view, std::wstring>::make_string(U"\U0001F600"sv);
    EXPECT_FALSE(result.empty());
    // The source codepoint must survive a round-trip through u32string
    auto back = m::string_converter<std::wstring, std::u32string>::make_string(result);
    EXPECT_EQ(back, U"\U0001F600"s);
}

TEST(Char32ToWstring, U32stringValue)
{
    auto result = m::string_converter<std::u32string, std::wstring>::make_string(U"test"s);
    EXPECT_EQ(result, L"test");
}

TEST(Char32ToWstring, OptionalNullopt)
{
    auto result = m::string_converter<std::u32string_view, std::wstring>::make_string(
        std::optional<std::u32string_view>{std::nullopt});
    EXPECT_FALSE(result.has_value());
}

TEST(WcharToU32string, NullRawPointer)
{
    auto result = m::string_converter<wchar_t const*, std::u32string>::make_string(nullptr);
    EXPECT_TRUE(result.empty());
}

TEST(WcharToU32string, Ascii)
{
    auto result = m::string_converter<wchar_t const*, std::u32string>::make_string(L"hello");
    EXPECT_EQ(result, U"hello");
}

TEST(WcharToU32string, TwoByteCodepoint)
{
    auto result = m::string_converter<std::wstring_view, std::u32string>::make_string(L"\u00E9"sv);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], U'\u00E9');
}

TEST(WcharToU32string, WstringValue)
{
    auto result = m::string_converter<std::wstring, std::u32string>::make_string(L"test"s);
    EXPECT_EQ(result, U"test");
}

// Round-trip: wchar ↔ char32
TEST(WcharU32RoundTrip, Ascii)
{
    std::wstring const original{L"hello world"};
    auto u32  = m::string_converter<std::wstring, std::u32string>::make_string(original);
    auto back = m::string_converter<std::u32string, std::wstring>::make_string(u32);
    EXPECT_EQ(back, original);
}

TEST(WcharU32RoundTrip, MultibyteCodepoints)
{
    std::wstring const original{L"\u00E9\u4E2D\U0001F600"};
    auto u32  = m::string_converter<std::wstring, std::u32string>::make_string(original);
    auto back = m::string_converter<std::u32string, std::wstring>::make_string(u32);
    EXPECT_EQ(back, original);
}

// ===========================================================================
// char32_t / std::u32string → std::string  (and back)
// ===========================================================================

TEST(Char32ToString, NullRawPointer)
{
    auto result = m::string_converter<char32_t const*, std::string>::make_string(nullptr);
    EXPECT_TRUE(result.empty());
}

TEST(Char32ToString, AsciiRawPointer)
{
    auto result = m::string_converter<char32_t const*, std::string>::make_string(U"hello");
    EXPECT_EQ(result, "hello");
}

TEST(Char32ToString, U32stringViewAscii)
{
    auto result = m::string_converter<std::u32string_view, std::string>::make_string(U"world"sv);
    EXPECT_EQ(result, "world");
}

TEST(Char32ToString, U32stringAscii)
{
    auto result = m::string_converter<std::u32string, std::string>::make_string(U"hello world"s);
    EXPECT_EQ(result, "hello world");
}

TEST(Char32ToString, OptionalNullopt)
{
    auto result = m::string_converter<std::u32string_view, std::string>::make_string(
        std::optional<std::u32string_view>{std::nullopt});
    EXPECT_FALSE(result.has_value());
}

TEST(CharToU32string, NullRawPointer)
{
    auto result = m::string_converter<char const*, std::u32string>::make_string(nullptr);
    EXPECT_TRUE(result.empty());
}

TEST(CharToU32string, AsciiRawPointer)
{
    auto result = m::string_converter<char const*, std::u32string>::make_string("hello");
    EXPECT_EQ(result, U"hello");
}

TEST(CharToU32string, StringViewAscii)
{
    auto result = m::string_converter<std::string_view, std::u32string>::make_string("world"sv);
    EXPECT_EQ(result, U"world");
}

TEST(CharToU32string, StringAscii)
{
    auto result = m::string_converter<std::string, std::u32string>::make_string("hello world"s);
    EXPECT_EQ(result, U"hello world");
}

TEST(CharToU32string, OptionalNullopt)
{
    auto result = m::string_converter<std::string_view, std::u32string>::make_string(
        std::optional<std::string_view>{std::nullopt});
    EXPECT_FALSE(result.has_value());
}

// Round-trip: ASCII char <-> char32
TEST(CharU32RoundTrip, Ascii)
{
    std::string const original{"round trip test"};
    auto              u32 = m::string_converter<std::string, std::u32string>::make_string(original);
    auto              back = m::string_converter<std::u32string, std::string>::make_string(u32);
    EXPECT_EQ(back, original);
}

// ===========================================================================
// char8_t / std::u8string → std::string  (and back)
// ===========================================================================

TEST(Char8ToString, NullRawPointer)
{
    auto result = m::string_converter<char8_t const*, std::string>::make_string(nullptr);
    EXPECT_TRUE(result.empty());
}

TEST(Char8ToString, AsciiRawPointer)
{
    auto result = m::string_converter<char8_t const*, std::string>::make_string(u8"hello");
    EXPECT_EQ(result, "hello");
}

TEST(Char8ToString, U8stringViewAscii)
{
    auto result = m::string_converter<std::u8string_view, std::string>::make_string(u8"world"sv);
    EXPECT_EQ(result, "world");
}

TEST(Char8ToString, U8stringAscii)
{
    auto result = m::string_converter<std::u8string, std::string>::make_string(u8"hello world"s);
    EXPECT_EQ(result, "hello world");
}

TEST(Char8ToString, OptionalNullopt)
{
    auto result = m::string_converter<std::u8string_view, std::string>::make_string(
        std::optional<std::u8string_view>{std::nullopt});
    EXPECT_FALSE(result.has_value());
}

TEST(Char8ToString, OptionalValue)
{
    auto result = m::string_converter<std::u8string_view, std::string>::make_string(
        std::optional<std::u8string_view>{u8"ok"sv});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "ok");
}

// ===========================================================================
// Empty string edge cases for all converter families
// ===========================================================================

TEST(EmptyStrings, CharToWstring)
{
    EXPECT_TRUE((m::string_converter<std::string_view, std::wstring>::make_string(""sv).empty()));
    EXPECT_TRUE((m::string_converter<std::string, std::wstring>::make_string(""s).empty()));
}

TEST(EmptyStrings, WcharToString)
{
    EXPECT_TRUE((m::string_converter<std::wstring_view, std::string>::make_string(L""sv).empty()));
    EXPECT_TRUE((m::string_converter<std::wstring, std::string>::make_string(L""s).empty()));
}

TEST(EmptyStrings, U8ToWstring)
{
    EXPECT_TRUE(
        (m::string_converter<std::u8string_view, std::wstring>::make_string(u8""sv).empty()));
    EXPECT_TRUE((m::string_converter<std::u8string, std::wstring>::make_string(u8""s).empty()));
}

TEST(EmptyStrings, WcharToU8string)
{
    EXPECT_TRUE(
        (m::string_converter<std::wstring_view, std::u8string>::make_string(L""sv).empty()));
    EXPECT_TRUE((m::string_converter<std::wstring, std::u8string>::make_string(L""s).empty()));
}

TEST(EmptyStrings, U16ToWstring)
{
    EXPECT_TRUE(
        (m::string_converter<std::u16string_view, std::wstring>::make_string(u""sv).empty()));
    EXPECT_TRUE((m::string_converter<std::u16string, std::wstring>::make_string(u""s).empty()));
}

TEST(EmptyStrings, WcharToU16string)
{
    EXPECT_TRUE(
        (m::string_converter<std::wstring_view, std::u16string>::make_string(L""sv).empty()));
    EXPECT_TRUE((m::string_converter<std::wstring, std::u16string>::make_string(L""s).empty()));
}

TEST(EmptyStrings, U32ToWstring)
{
    EXPECT_TRUE(
        (m::string_converter<std::u32string_view, std::wstring>::make_string(U""sv).empty()));
    EXPECT_TRUE((m::string_converter<std::u32string, std::wstring>::make_string(U""s).empty()));
}

TEST(EmptyStrings, WcharToU32string)
{
    EXPECT_TRUE(
        (m::string_converter<std::wstring_view, std::u32string>::make_string(L""sv).empty()));
    EXPECT_TRUE((m::string_converter<std::wstring, std::u32string>::make_string(L""s).empty()));
}
