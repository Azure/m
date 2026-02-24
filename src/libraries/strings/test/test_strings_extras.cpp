// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/strings/compare.h>
#include <m/strings/convert.h>
#include <m/strings/literal_string_view.h>
#include <m/strings/ordinal_compare.h>
#include <m/strings/split.h>
#include <m/strings/static_string.h>

using namespace std::string_literals;
using namespace std::string_view_literals;
using namespace m::string_view_literals;

// ===========================================================================
// split_basic_string tests
// ===========================================================================

TEST(Split, EmptyInputNoDelimiter)
{
    auto const r = m::split_basic_string<char>(""sv, ',');
    EXPECT_EQ(r.item, ""sv);
    EXPECT_FALSE(r.remainder.has_value());
}

TEST(Split, NoDelimiterFound)
{
    auto const r = m::split_basic_string<char>("hello"sv, ',');
    EXPECT_EQ(r.item, "hello"sv);
    EXPECT_FALSE(r.remainder.has_value());
}

TEST(Split, DelimiterInMiddle)
{
    auto const r = m::split_basic_string<char>("hello,world"sv, ',');
    EXPECT_EQ(r.item, "hello"sv);
    ASSERT_TRUE(r.remainder.has_value());
    EXPECT_EQ(*r.remainder, "world"sv);
}

TEST(Split, DelimiterAtFront)
{
    // Delimiter as first character: item is empty, remainder is the rest.
    auto const r = m::split_basic_string<char>(",rest"sv, ',');
    EXPECT_EQ(r.item, ""sv);
    ASSERT_TRUE(r.remainder.has_value());
    EXPECT_EQ(*r.remainder, "rest"sv);
}

TEST(Split, DelimiterAtEnd)
{
    // Delimiter as last character: item is "first", remainder is an empty
    // string_view (not nullopt — the caller can tell there was a trailing
    // delimiter).
    auto const r = m::split_basic_string<char>("first,"sv, ',');
    EXPECT_EQ(r.item, "first"sv);
    ASSERT_TRUE(r.remainder.has_value());
    EXPECT_EQ(*r.remainder, ""sv);
}

TEST(Split, OnlyDelimiter)
{
    auto const r = m::split_basic_string<char>(","sv, ',');
    EXPECT_EQ(r.item, ""sv);
    ASSERT_TRUE(r.remainder.has_value());
    EXPECT_EQ(*r.remainder, ""sv);
}

TEST(Split, MultipleDelimiters_ChainedSplit)
{
    // Simulate iterating over "a,b,c,d" using repeated splitting.
    std::string_view input = "a,b,c,d"sv;

    auto r1 = m::split_basic_string<char>(input, ',');
    EXPECT_EQ(r1.item, "a"sv);
    ASSERT_TRUE(r1.remainder.has_value());

    auto r2 = m::split_basic_string<char>(*r1.remainder, ',');
    EXPECT_EQ(r2.item, "b"sv);
    ASSERT_TRUE(r2.remainder.has_value());

    auto r3 = m::split_basic_string<char>(*r2.remainder, ',');
    EXPECT_EQ(r3.item, "c"sv);
    ASSERT_TRUE(r3.remainder.has_value());

    auto r4 = m::split_basic_string<char>(*r3.remainder, ',');
    EXPECT_EQ(r4.item, "d"sv);
    EXPECT_FALSE(r4.remainder.has_value());
}

TEST(Split, ConsecutiveDelimiters)
{
    // "a,,b" — middle empty token from two consecutive delimiters.
    auto const r1 = m::split_basic_string<char>("a,,b"sv, ',');
    EXPECT_EQ(r1.item, "a"sv);
    ASSERT_TRUE(r1.remainder.has_value());

    auto const r2 = m::split_basic_string<char>(*r1.remainder, ',');
    EXPECT_EQ(r2.item, ""sv);
    ASSERT_TRUE(r2.remainder.has_value());
    EXPECT_EQ(*r2.remainder, "b"sv);
}

TEST(Split, WideString)
{
    auto const r = m::split_basic_string<wchar_t>(L"left|right"sv, L'|');
    EXPECT_EQ(r.item, L"left"sv);
    ASSERT_TRUE(r.remainder.has_value());
    EXPECT_EQ(*r.remainder, L"right"sv);
}

TEST(Split, WideStringNoDelimiter)
{
    auto const r = m::split_basic_string<wchar_t>(L"nodelimiter"sv, L'|');
    EXPECT_EQ(r.item, L"nodelimiter"sv);
    EXPECT_FALSE(r.remainder.has_value());
}

TEST(Split, U16String)
{
    auto const r = m::split_basic_string<char16_t>(u"hello/world"sv, u'/');
    EXPECT_EQ(r.item, u"hello"sv);
    ASSERT_TRUE(r.remainder.has_value());
    EXPECT_EQ(*r.remainder, u"world"sv);
}

TEST(Split, ItemAndRemainderAreViews_SameUnderlyingData)
{
    // The item and remainder views should point into the original string data,
    // not copies.
    std::string_view input = "abc,def"sv;
    auto const       r     = m::split_basic_string<char>(input, ',');
    EXPECT_EQ(r.item.data(), input.data());             // same pointer for item
    EXPECT_EQ(r.remainder->data(), input.data() + 4);  // remainder starts after ','
}

// ===========================================================================
// static_string tests
// ===========================================================================

TEST(StaticString, StrReturnsNullTerminatedPointer)
{
    constexpr m::static_string s("hello");
    EXPECT_EQ(std::string_view(s.str()), "hello"sv);
}

TEST(StaticString, ViewReturnsCorrectView)
{
    constexpr m::static_string s("hello");
    EXPECT_EQ(s.view(), "hello"sv);
}

TEST(StaticString, ViewLengthExcludesNullTerminator)
{
    constexpr m::static_string s("hello");
    EXPECT_EQ(s.view().size(), 5u);
}

TEST(StaticString, StrAndViewShareSameData)
{
    constexpr m::static_string s("hello");
    EXPECT_EQ(s.view().data(), s.str());
}

TEST(StaticString, EmptyString)
{
    constexpr m::static_string s("");
    EXPECT_EQ(s.view(), ""sv);
    EXPECT_EQ(s.view().size(), 0u);
    EXPECT_EQ(*s.str(), '\0');
}

TEST(StaticString, WideString)
{
    constexpr m::static_string s(L"world");
    EXPECT_EQ(s.view(), L"world"sv);
    EXPECT_EQ(s.view().size(), 5u);
}

TEST(StaticString, IsConstexpr)
{
    static constexpr m::static_string s("constexpr");
    constexpr auto                    v = s.view();
    EXPECT_EQ(v, "constexpr"sv);
}

TEST(StaticString, NullTerminatorPresentInData)
{
    constexpr m::static_string s("abc");
    // N is 4 (3 chars + null). The array should contain 'a','b','c','\0'.
    EXPECT_EQ(s.m_data[0], 'a');
    EXPECT_EQ(s.m_data[1], 'b');
    EXPECT_EQ(s.m_data[2], 'c');
    EXPECT_EQ(s.m_data[3], '\0');
}

// ===========================================================================
// ordinal_compare spaceship() tests
// (regression for the r > l copy-paste bug where greater was unreachable)
// ===========================================================================

TEST(OrdinalCompare, SpaceshipLess)
{
    auto const r = m::strings::spaceship(char32_t{U'a'}, char32_t{U'b'});
    EXPECT_EQ(r, std::strong_ordering::less);
}

TEST(OrdinalCompare, SpaceshipGreater)
{
    // This case could NOT be reached before the bug fix (the second condition
    // was `r > l` which is equivalent to `l < r`, the same as the first).
    auto const r = m::strings::spaceship(char32_t{U'b'}, char32_t{U'a'});
    EXPECT_EQ(r, std::strong_ordering::greater);
}

TEST(OrdinalCompare, SpaceshipEquivalent)
{
    auto const r = m::strings::spaceship(char32_t{U'x'}, char32_t{U'x'});
    EXPECT_EQ(r, std::strong_ordering::equivalent);
}

TEST(OrdinalCompare, SpaceshipZeroVsMax)
{
    auto const r = m::strings::spaceship(char32_t{0}, char32_t{0x10FFFF});
    EXPECT_EQ(r, std::strong_ordering::less);

    auto const r2 = m::strings::spaceship(char32_t{0x10FFFF}, char32_t{0});
    EXPECT_EQ(r2, std::strong_ordering::greater);
}

// ===========================================================================
// literal_string_view tests (expanding the single existing test)
// ===========================================================================

TEST(LiteralStringView, NarrowChar)
{
    auto l1 = "hello"_sl;
    EXPECT_EQ("hello"sv, l1);
    static_assert(std::is_same_v<decltype(l1), m::literal_string_view>);
}

TEST(LiteralStringView, WideChar)
{
    auto l1 = L"hello"_sl;
    EXPECT_EQ(L"hello"sv, l1);
    static_assert(std::is_same_v<decltype(l1), m::wliteral_string_view>);
}

TEST(LiteralStringView, Char16)
{
    auto l1 = u"hello"_sl;
    EXPECT_EQ(u"hello"sv, l1);
    static_assert(std::is_same_v<decltype(l1), m::u16literal_string_view>);
}

TEST(LiteralStringView, Char32)
{
    auto l1 = U"hello"_sl;
    EXPECT_EQ(U"hello"sv, l1);
    static_assert(std::is_same_v<decltype(l1), m::u32literal_string_view>);
}

#ifdef __cpp_char8_t
TEST(LiteralStringView, Char8)
{
    auto l1 = u8"hello"_sl;
    EXPECT_EQ(u8"hello"sv, l1);
    static_assert(std::is_same_v<decltype(l1), m::u8literal_string_view>);
}

TEST(LiteralStringView, EmptyChar8)
{
    auto l1 = u8""_sl;
    EXPECT_EQ(u8""sv, l1);
    EXPECT_EQ(l1.size(), 0u);
}
#endif

TEST(LiteralStringView, EmptyNarrow)
{
    auto l1 = ""_sl;
    EXPECT_EQ(""sv, l1);
    EXPECT_EQ(l1.size(), 0u);
}

TEST(LiteralStringView, EmptyWide)
{
    auto l1 = L""_sl;
    EXPECT_EQ(L""sv, l1);
    EXPECT_EQ(l1.size(), 0u);
}

TEST(LiteralStringView, InheritsFromStringView)
{
    auto l1 = "test"_sl;
    // Should be usable anywhere std::string_view is expected.
    std::string_view sv = l1;
    EXPECT_EQ(sv, "test"sv);
}

TEST(LiteralStringView, SizeMatchesLiteral)
{
    auto l1 = "abc"_sl;
    EXPECT_EQ(l1.size(), 3u);
}

// ===========================================================================
// case_insensitive_less — narrow-string tests (not covered by existing suite)
// ===========================================================================

namespace
{
    auto const lt_s = m::case_insensitive_less<std::string>{};
}

TEST(Strings_Compare_narrow, Less) { EXPECT_TRUE(lt_s("apple"s, "banana"s)); }
TEST(Strings_Compare_narrow, Greater) { EXPECT_FALSE(lt_s("banana"s, "apple"s)); }
TEST(Strings_Compare_narrow, EqualSameCase) { EXPECT_FALSE(lt_s("banana"s, "banana"s)); }
TEST(Strings_Compare_narrow, EqualMixedCase) { EXPECT_FALSE(lt_s("Banana"s, "banana"s)); }
TEST(Strings_Compare_narrow, LessMixedCase) { EXPECT_TRUE(lt_s("apple"s, "BANANA"s)); }

TEST(Strings_Compare_narrow, EmptyLessThanNonEmpty) { EXPECT_TRUE(lt_s(""s, "a"s)); }
TEST(Strings_Compare_narrow, EmptyNotLessThanEmpty) { EXPECT_FALSE(lt_s(""s, ""s)); }
TEST(Strings_Compare_narrow, NonEmptyNotLessThanEmpty) { EXPECT_FALSE(lt_s("a"s, ""s)); }

TEST(Strings_Compare_narrow, StringViewArgs)
{
    EXPECT_TRUE(lt_s("apple"sv, "BANANA"sv));
    EXPECT_FALSE(lt_s("BANANA"sv, "apple"sv));
}

// ===========================================================================
// to_sstring / to_wsstring / to_u8sstring / to_u16sstring / to_u32sstring
// ===========================================================================

TEST(ToSString, NarrowFromNarrowView)
{
    auto const s = m::to_sstring("hello"sv);
    EXPECT_EQ(s.view(), "hello"sv);
}

TEST(ToSString, EmptyNarrow)
{
    auto const s = m::to_sstring(""sv);
    EXPECT_EQ(s.view(), ""sv);
}

TEST(ToWsstring, WideFromWideView)
{
    auto const s = m::to_wsstring(L"hello"sv);
    EXPECT_EQ(s.view(), L"hello"sv);
}

TEST(ToWsstring, EmptyWide)
{
    auto const s = m::to_wsstring(L""sv);
    EXPECT_EQ(s.view(), L""sv);
}

TEST(ToU8sstring, U8FromU8View)
{
    auto const s = m::to_u8sstring(u8"hello"sv);
    EXPECT_EQ(s.view(), u8"hello"sv);
}

TEST(ToU16sstring, U16FromU16View)
{
    auto const s = m::to_u16sstring(u"hello"sv);
    EXPECT_EQ(s.view(), u"hello"sv);
}

TEST(ToU32sstring, U32FromU32View)
{
    auto const s = m::to_u32sstring(U"hello"sv);
    EXPECT_EQ(s.view(), U"hello"sv);
}
