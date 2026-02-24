// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <chrono>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <m/sstring/sstring.h>
#include <m/strings/compare.h>
#include <m/test_data/test_data.h>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

// ============================================================================
// Legacy tests (kept for regression)
// ============================================================================

TEST(TestSString, SimpleAssign) { m::wsstring x{L"foo"}; }

TEST(TestSString, TryConcat)
{
    m::wsstring x{L"foo"sv};
    m::wsstring y{L"bar"sv};
    auto        z = x + y;
    m::wsstring e(L"foobar"sv);

    EXPECT_EQ(z, e);
}

TEST(TestSString, TestAddWithNatoLetters1)
{
    auto x = m::sstring("foo"sv);

    for (auto const& e: m::test_data::nato_alphabet_sv)
        x = x + e;

    EXPECT_EQ(
        x,
        "fooAlfaBravoCharlieDeltaEchoFoxtrotGolfHotelIndiaJuliettKiloLimaMikeNovemberOscarPapaQuebecRomeoSierraTangoUniformVictorWhiskeyXrayYankeeZulu");
}

TEST(TestSString, TestSubstr)
{
    auto x = m::sstring("foo"sv);

    for (auto const& e: m::test_data::nato_alphabet_sv)
        x = x + e;

    auto y = x.substr(20, 5);
    EXPECT_EQ(y, "eltaE");
}

TEST(TestSString, TestLeft)
{
    auto x = m::sstring(m::test_data::alpha_num_sv);
    auto y = x.left(5);
    EXPECT_EQ(y, "abcde");
}

TEST(TestSString, TestRight)
{
    auto x = m::sstring(m::test_data::alpha_num_sv);
    auto y = x.right(7);
    EXPECT_EQ(y, "3456789");
}

TEST(TestSString, TestCaseInsensitiveLess)
{
    auto x = m::sstring(m::test_data::alpha_num_sv);
    auto y = std::string("flarb");

    auto z = m::case_insensitive_less<m::sstring>{};
    EXPECT_EQ(z(y, x), false);
}

// ============================================================================
// Default construction — empty / null state
// ============================================================================

TEST(SString_Default, DefaultIsEmpty)
{
    m::sstring s;
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.view().size(), 0u);
    EXPECT_EQ(s.view(), ""sv);
}

TEST(SString_Default, DefaultEqualsLiteralEmpty)
{
    m::sstring s;
    EXPECT_EQ(s, ""sv);
    EXPECT_EQ(s, "");
    EXPECT_EQ(s, ""s);
}

TEST(SString_Default, DefaultCStrIsNullTerminated)
{
    m::sstring s;
    auto const cs = s.c_str();
    EXPECT_NE(cs, nullptr);
    EXPECT_EQ(cs[0], '\0');
}

// ============================================================================
// Construction from string_view
// ============================================================================

TEST(SString_Construct, FromStringView)
{
    m::sstring s("hello"sv);
    EXPECT_EQ(s.view(), "hello"sv);
    EXPECT_FALSE(s.empty());
}

TEST(SString_Construct, FromEmptyStringView)
{
    m::sstring s(""sv);
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.view(), ""sv);
}

TEST(SString_Construct, FromStdString)
{
    std::string src = "world";
    m::sstring  s(src);
    EXPECT_EQ(s.view(), "world"sv);
}

TEST(SString_Construct, FromEmptyStdString)
{
    std::string src;
    m::sstring  s(src);
    EXPECT_TRUE(s.empty());
}

TEST(SString_Construct, FromInitializerList)
{
    m::sstring s({"foo"sv, "bar"sv, "baz"sv});
    EXPECT_EQ(s.view(), "foobarbaz"sv);
}

TEST(SString_Construct, FromInitializerListWithEmpty)
{
    // Concatenating with empty pieces must not alter the result.
    m::sstring s({"foo"sv, ""sv, "bar"sv});
    EXPECT_EQ(s.view(), "foobar"sv);
}

TEST(SString_Construct, FromSpanOfViews)
{
    std::vector<std::string_view> pieces = {"a"sv, "b"sv, "c"sv};
    m::sstring s(std::span<std::string_view const>{pieces.data(), pieces.size()});
    EXPECT_EQ(s.view(), "abc"sv);
}

// ============================================================================
// Wide / Unicode character types
// ============================================================================

TEST(SString_Wide, ConstructFromWStringView)
{
    m::wsstring s(L"hello"sv);
    EXPECT_EQ(s.view(), L"hello"sv);
}

TEST(SString_Wide, ConstructFromU8StringView)
{
    m::u8sstring s(u8"hello"sv);
    EXPECT_EQ(s.view(), u8"hello"sv);
}

TEST(SString_Wide, ConstructFromU16StringView)
{
    m::u16sstring s(u"hello"sv);
    EXPECT_EQ(s.view(), u"hello"sv);
}

TEST(SString_Wide, ConstructFromU32StringView)
{
    m::u32sstring s(U"hello"sv);
    EXPECT_EQ(s.view(), U"hello"sv);
}

// ============================================================================
// Copy construction and assignment
// ============================================================================

TEST(SString_Copy, CopyConstruct)
{
    m::sstring a("copy me"sv);
    m::sstring b(a);
    EXPECT_EQ(b.view(), "copy me"sv);
    // Must share the same underlying data pointer (i.e. no deep copy).
    EXPECT_EQ(a.view().data(), b.view().data());
}

TEST(SString_Copy, CopyConstructFromEmpty)
{
    m::sstring a;
    m::sstring b(a);
    EXPECT_TRUE(b.empty());
}

TEST(SString_Copy, CopyAssign)
{
    m::sstring a("assign me"sv);
    m::sstring b;
    b = a;
    EXPECT_EQ(b.view(), "assign me"sv);
    EXPECT_EQ(a.view().data(), b.view().data());
}

TEST(SString_Copy, CopyAssignSelf)
{
    m::sstring a("self"sv);
    // Assign through a reference to suppress the clang -Wself-assign-overloaded
    // diagnostic while still exercising the self-assignment code path.
    auto& aref = a;
    a = aref;
    EXPECT_EQ(a.view(), "self"sv);
}

TEST(SString_Copy, CopyAssignOverwritesExisting)
{
    m::sstring a("first"sv);
    m::sstring b("second"sv);
    b = a;
    EXPECT_EQ(b.view(), "first"sv);
}

// ============================================================================
// Move construction and assignment
// ============================================================================

TEST(SString_Move, MoveConstruct)
{
    m::sstring a("move me"sv);
    m::sstring b(std::move(a));
    EXPECT_EQ(b.view(), "move me"sv);
    EXPECT_TRUE(a.empty()); // moved-from must be empty
}

TEST(SString_Move, MoveConstructFromEmpty)
{
    m::sstring a;
    m::sstring b(std::move(a));
    EXPECT_TRUE(b.empty());
    EXPECT_TRUE(a.empty());
}

TEST(SString_Move, MoveAssign)
{
    m::sstring a("move assign"sv);
    m::sstring b;
    b = std::move(a);
    EXPECT_EQ(b.view(), "move assign"sv);
    EXPECT_TRUE(a.empty());
}

TEST(SString_Move, MoveAssignOverwritesExisting)
{
    m::sstring a("new"sv);
    m::sstring b("old"sv);
    b = std::move(a);
    EXPECT_EQ(b.view(), "new"sv);
    // The move-assignment operator is swap-based: after `b = std::move(a)` the
    // moved-from object `a` holds b's former contents ("old"), not the empty string.
    // The C++ standard only requires a valid-but-unspecified state after a move.
    EXPECT_EQ(a.view(), "old"sv);
}

// ============================================================================
// operator+= and operator+
// ============================================================================

TEST(SString_Concat, PlusEqualsBasic)
{
    m::sstring a("foo"sv);
    m::sstring b("bar"sv);
    a += b;
    EXPECT_EQ(a.view(), "foobar"sv);
}

TEST(SString_Concat, PlusEqualsEmpty)
{
    m::sstring a("foo"sv);
    m::sstring empty;
    a += empty;
    EXPECT_EQ(a.view(), "foo"sv);
}

TEST(SString_Concat, PlusEmptyPlusNonEmpty)
{
    m::sstring a;
    m::sstring b("bar"sv);
    // Adding empty + non-empty should return b exactly (same ptr).
    auto c = a + b;
    EXPECT_EQ(c.view(), "bar"sv);
}

TEST(SString_Concat, PlusNonEmptyPlusEmpty)
{
    m::sstring a("foo"sv);
    m::sstring b;
    auto       c = a + b;
    EXPECT_EQ(c.view(), "foo"sv);
}

TEST(SString_Concat, PlusStringView)
{
    m::sstring a("foo"sv);
    auto       b = a + "bar"sv;
    EXPECT_EQ(b.view(), "foobar"sv);
}

TEST(SString_Concat, PlusLhsStringView)
{
    // Tests the free operator+(view_type, basic_sstring const&)
    m::sstring r("bar"sv);
    auto       result = "foo"sv + r;
    EXPECT_EQ(result.view(), "foobar"sv);
}

TEST(SString_Concat, ChainedConcat)
{
    m::sstring a("a"sv);
    m::sstring b("b"sv);
    m::sstring c("c"sv);
    auto       d = a + b + c;
    EXPECT_EQ(d.view(), "abc"sv);
}

// ============================================================================
// c_str()
// ============================================================================

TEST(SString_CStr, NonEmptyIsNullTerminated)
{
    m::sstring s("hello"sv);
    auto const cs = s.c_str();
    EXPECT_EQ(std::string_view(cs), "hello"sv);
    EXPECT_EQ(cs[5], '\0');
}

TEST(SString_CStr, EmptyIsNullTerminated)
{
    m::sstring s;
    auto const cs = s.c_str();
    EXPECT_NE(cs, nullptr);
    EXPECT_EQ(cs[0], '\0');
}

TEST(SString_CStr, SubstrCStrIsNullTerminated)
{
    // substr() of a non-suffix portion cannot share the null terminator from
    // the backing store, so c_str() must allocate a separate null-terminated copy.
    m::sstring s("hello world"sv);
    auto       sub = s.substr(0, 5); // "hello" — not at the end of backing store
    auto const cs  = sub.c_str();
    EXPECT_EQ(std::string_view(cs), "hello"sv);
    EXPECT_EQ(cs[5], '\0');
}

TEST(SString_CStr, SuffixSubstrSharesCStr)
{
    // A substr that reaches the end of the backing store CAN share the null
    // terminator; verify the value is still correct.
    m::sstring s("hello world"sv);
    auto       sub = s.substr(6); // "world" — suffix
    auto const cs  = sub.c_str();
    EXPECT_EQ(std::string_view(cs), "world"sv);
}

TEST(SString_CStr, RepeatedCallReturnsSamePtr)
{
    m::sstring s("repeated"sv);
    EXPECT_EQ(s.c_str(), s.c_str());
}

// ============================================================================
// view() and operator view_type
// ============================================================================

TEST(SString_View, ViewMatchesConstruction)
{
    m::sstring s("viewtest"sv);
    EXPECT_EQ(s.view(), "viewtest"sv);
    std::string_view sv = s; // implicit conversion via operator view_type
    EXPECT_EQ(sv, "viewtest"sv);
}

TEST(SString_View, ViewOfEmpty)
{
    m::sstring s;
    EXPECT_EQ(s.view(), ""sv);
}

// ============================================================================
// substr()
// ============================================================================

TEST(SString_Substr, Basic)
{
    m::sstring s("abcdef"sv);
    EXPECT_EQ(s.substr(2, 3).view(), "cde"sv);
}

TEST(SString_Substr, FromBeginning)
{
    m::sstring s("abcdef"sv);
    EXPECT_EQ(s.substr(0, 3).view(), "abc"sv);
}

TEST(SString_Substr, ToEnd)
{
    m::sstring s("abcdef"sv);
    EXPECT_EQ(s.substr(3).view(), "def"sv);
}

TEST(SString_Substr, EntireString)
{
    m::sstring s("abcdef"sv);
    EXPECT_EQ(s.substr(0).view(), "abcdef"sv);
}

TEST(SString_Substr, ZeroLength)
{
    m::sstring s("abcdef"sv);
    EXPECT_TRUE(s.substr(2, 0).empty());
}

TEST(SString_Substr, LengthClampsAtEnd)
{
    // Requesting more characters than remain should clamp to end-of-string.
    m::sstring s("abcdef"sv);
    EXPECT_EQ(s.substr(3, 1000).view(), "def"sv);
}

TEST(SString_Substr, StartAtEnd)
{
    // start == size() is valid and returns empty.
    m::sstring s("abc"sv);
    EXPECT_TRUE(s.substr(3).empty());
}

TEST(SString_Substr, PastEndThrows)
{
    m::sstring s("abc"sv);
    EXPECT_THROW(s.substr(4), std::out_of_range);
}

TEST(SString_Substr, OnEmpty)
{
    m::sstring s;
    EXPECT_TRUE(s.substr(0).empty());
    EXPECT_THROW(s.substr(1), std::out_of_range);
}

// ============================================================================
// left() and right()
// ============================================================================

TEST(SString_Left, Basic)
{
    m::sstring s("abcdef"sv);
    EXPECT_EQ(s.left(3).view(), "abc"sv);
}

TEST(SString_Left, Zero)
{
    m::sstring s("abcdef"sv);
    EXPECT_TRUE(s.left(0).empty());
}

TEST(SString_Left, ExceedsSize)
{
    // left() should clamp to the string length without throwing.
    m::sstring s("abc"sv);
    EXPECT_EQ(s.left(100).view(), "abc"sv);
}

TEST(SString_Left, ExactSize)
{
    m::sstring s("abc"sv);
    EXPECT_EQ(s.left(3).view(), "abc"sv);
}

TEST(SString_Left, OnEmpty)
{
    m::sstring s;
    EXPECT_TRUE(s.left(0).empty());
    EXPECT_TRUE(s.left(5).empty()); // clamp
}

TEST(SString_Right, Basic)
{
    m::sstring s("abcdef"sv);
    EXPECT_EQ(s.right(3).view(), "def"sv);
}

TEST(SString_Right, Zero)
{
    m::sstring s("abcdef"sv);
    EXPECT_TRUE(s.right(0).empty());
}

TEST(SString_Right, ExceedsSize)
{
    m::sstring s("abc"sv);
    EXPECT_EQ(s.right(100).view(), "abc"sv);
}

TEST(SString_Right, ExactSize)
{
    m::sstring s("abc"sv);
    EXPECT_EQ(s.right(3).view(), "abc"sv);
}

TEST(SString_Right, OnEmpty)
{
    m::sstring s;
    EXPECT_TRUE(s.right(0).empty());
    EXPECT_TRUE(s.right(5).empty()); // clamp
}

// ============================================================================
// operator[] / first() / last()
// ============================================================================

TEST(SString_Index, OperatorBracket)
{
    m::sstring s("abc"sv);
    EXPECT_EQ(s[0], 'a');
    EXPECT_EQ(s[1], 'b');
    EXPECT_EQ(s[2], 'c');
}

TEST(SString_Index, First)
{
    m::sstring s("xyz"sv);
    EXPECT_EQ(s.first(), 'x');
}

TEST(SString_Index, Last)
{
    m::sstring s("xyz"sv);
    EXPECT_EQ(s.last(), 'z');
}

TEST(SString_Index, SingleChar)
{
    m::sstring s("Q"sv);
    EXPECT_EQ(s.first(), 'Q');
    EXPECT_EQ(s.last(), 'Q');
    EXPECT_EQ(s[0], 'Q');
}

// ============================================================================
// contains() / find_first_of() / find_last_of() / try_ variants
// ============================================================================

TEST(SString_Find, ContainsTrue)
{
    m::sstring s("hello"sv);
    EXPECT_TRUE(s.contains('e'));
}

TEST(SString_Find, ContainsFalse)
{
    m::sstring s("hello"sv);
    EXPECT_FALSE(s.contains('z'));
}

TEST(SString_Find, ContainsOnEmpty)
{
    m::sstring s;
    EXPECT_FALSE(s.contains('a'));
}

TEST(SString_Find, FindFirstOf)
{
    m::sstring s("abcabc"sv);
    EXPECT_EQ(s.find_first_of('b'), 1u);
}

TEST(SString_Find, FindFirstOfNotFound)
{
    m::sstring s("abc"sv);
    EXPECT_EQ(s.find_first_of('z'), m::sstring::npos);
}

TEST(SString_Find, FindLastOf)
{
    m::sstring s("abcabc"sv);
    EXPECT_EQ(s.find_last_of('b'), 4u);
}

TEST(SString_Find, FindLastOfNotFound)
{
    m::sstring s("abc"sv);
    EXPECT_EQ(s.find_last_of('z'), m::sstring::npos);
}

TEST(SString_Find, TryFindFirstOfFound)
{
    m::sstring s("hello"sv);
    auto       r = s.try_find_first_of('l');
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, 2u);
}

TEST(SString_Find, TryFindFirstOfNotFound)
{
    m::sstring s("hello"sv);
    EXPECT_FALSE(s.try_find_first_of('z').has_value());
}

TEST(SString_Find, TryFindLastOfFound)
{
    m::sstring s("hello"sv);
    auto       r = s.try_find_last_of('l');
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, 3u);
}

TEST(SString_Find, TryFindLastOfNotFound)
{
    m::sstring s("hello"sv);
    EXPECT_FALSE(s.try_find_last_of('z').has_value());
}

// ============================================================================
// split_at(char)
// ============================================================================

TEST(SString_SplitAtChar, Basic)
{
    m::sstring s("foo:bar"sv);
    auto [l, r] = s.split_at(':');
    EXPECT_EQ(l.view(), "foo"sv);
    EXPECT_EQ(r.view(), "bar"sv);
}

TEST(SString_SplitAtChar, CharAtStart)
{
    m::sstring s(":bar"sv);
    auto [l, r] = s.split_at(':');
    EXPECT_TRUE(l.empty());
    EXPECT_EQ(r.view(), "bar"sv);
}

TEST(SString_SplitAtChar, CharAtEnd)
{
    m::sstring s("foo:"sv);
    auto [l, r] = s.split_at(':');
    EXPECT_EQ(l.view(), "foo"sv);
    EXPECT_TRUE(r.empty());
}

TEST(SString_SplitAtChar, CharNotFound)
{
    // When the char is not found, left == whole string, right == empty.
    m::sstring s("foobar"sv);
    auto [l, r] = s.split_at(':');
    EXPECT_EQ(l.view(), "foobar"sv);
    EXPECT_TRUE(r.empty());
}

TEST(SString_SplitAtChar, OnlyTheChar)
{
    m::sstring s(":"sv);
    auto [l, r] = s.split_at(':');
    EXPECT_TRUE(l.empty());
    EXPECT_TRUE(r.empty());
}

TEST(SString_SplitAtChar, SplitsOnFirstOccurrence)
{
    // split_at(char) must split on the first occurrence.
    m::sstring s("a:b:c"sv);
    auto [l, r] = s.split_at(':');
    EXPECT_EQ(l.view(), "a"sv);
    EXPECT_EQ(r.view(), "b:c"sv);
}

// ============================================================================
// split_at(view_type)
// ============================================================================

TEST(SString_SplitAtView, Basic)
{
    m::sstring s("foo::bar"sv);
    auto [l, r] = s.split_at("::"sv);
    EXPECT_EQ(l.view(), "foo"sv);
    EXPECT_EQ(r.view(), "bar"sv);
}

TEST(SString_SplitAtView, NotFound)
{
    m::sstring s("foobar"sv);
    auto [l, r] = s.split_at("::"sv);
    EXPECT_EQ(l.view(), "foobar"sv);
    EXPECT_TRUE(r.empty());
}

TEST(SString_SplitAtView, DelimAtStart)
{
    m::sstring s("::rest"sv);
    auto [l, r] = s.split_at("::"sv);
    EXPECT_TRUE(l.empty());
    EXPECT_EQ(r.view(), "rest"sv);
}

TEST(SString_SplitAtView, DelimAtEnd)
{
    m::sstring s("start::"sv);
    auto [l, r] = s.split_at("::"sv);
    EXPECT_EQ(l.view(), "start"sv);
    EXPECT_TRUE(r.empty());
}

// ============================================================================
// split_at_first_of(view_type)
// ============================================================================

TEST(SString_SplitAtFirstOf, Basic)
{
    // Splits at the first character that appears in the charset.
    m::sstring s("foo:bar"sv);
    auto [l, r] = s.split_at_first_of(":;"sv);
    EXPECT_EQ(l.view(), "foo"sv);
    EXPECT_EQ(r.view(), "bar"sv);
}

TEST(SString_SplitAtFirstOf, NotFound)
{
    m::sstring s("foobar"sv);
    auto [l, r] = s.split_at_first_of(":;"sv);
    EXPECT_EQ(l.view(), "foobar"sv);
    EXPECT_TRUE(r.empty());
}

// ============================================================================
// equals()
// ============================================================================

TEST(SString_Equals, SameValue)
{
    m::sstring a("hello"sv);
    m::sstring b("hello"sv);
    EXPECT_TRUE(a.equals(b));
}

TEST(SString_Equals, DifferentValue)
{
    m::sstring a("hello"sv);
    m::sstring b("world"sv);
    EXPECT_FALSE(a.equals(b));
}

TEST(SString_Equals, BothEmpty)
{
    m::sstring a;
    m::sstring b;
    EXPECT_TRUE(a.equals(b));
}

// ============================================================================
// compare()
// ============================================================================

TEST(SString_Compare, EqualStrings)
{
    m::sstring s("abc"sv);
    EXPECT_EQ(s.compare("abc"sv), 0);
    EXPECT_EQ(s.compare(s), 0);
}

TEST(SString_Compare, LessThan)
{
    m::sstring s("abc"sv);
    EXPECT_LT(s.compare("abd"sv), 0);
}

TEST(SString_Compare, GreaterThan)
{
    m::sstring s("abd"sv);
    EXPECT_GT(s.compare("abc"sv), 0);
}

TEST(SString_Compare, WithPos)
{
    m::sstring s("abcdef"sv);
    // Compare 3 chars starting at offset 1 ("bcd") against "bcd"
    EXPECT_EQ(s.compare(1, 3, "bcd"sv), 0);
}

TEST(SString_Compare, WithPosAndPosInOther)
{
    m::sstring a("xxabcxx"sv);
    m::sstring b("yyabcyy"sv);
    // Compare "abc" (positions 2..4 of a) against "abc" (positions 2..4 of b)
    EXPECT_EQ(a.compare(2, 3, b.view(), 2, 3), 0);
}

// ============================================================================
// operator== and operator<=>
// ============================================================================

TEST(SString_Comparison, EqualToView)
{
    m::sstring s("hello"sv);
    EXPECT_EQ(s, "hello"sv);
    EXPECT_EQ(s, "hello"s);
    EXPECT_EQ(s, "hello");
}

TEST(SString_Comparison, NotEqual)
{
    m::sstring s("hello"sv);
    EXPECT_NE(s, "world"sv);
}

TEST(SString_Comparison, SpaceshipEqual)
{
    m::sstring a("abc"sv);
    m::sstring b("abc"sv);
    EXPECT_EQ(a <=> b, std::weak_ordering::equivalent);
}

TEST(SString_Comparison, SpaceshipLess)
{
    m::sstring a("abc"sv);
    m::sstring b("abd"sv);
    EXPECT_EQ(a <=> b, std::weak_ordering::less);
}

TEST(SString_Comparison, SpaceshipGreater)
{
    m::sstring a("abd"sv);
    m::sstring b("abc"sv);
    EXPECT_EQ(a <=> b, std::weak_ordering::greater);
}

TEST(SString_Comparison, SpaceshipVsView)
{
    m::sstring a("abc"sv);
    EXPECT_EQ(a <=> "abc"sv, std::weak_ordering::equivalent);
    EXPECT_EQ(a <=> "abd"sv, std::weak_ordering::less);
}

TEST(SString_Comparison, SpaceshipVsRawPtr)
{
    m::sstring a("abc"sv);
    EXPECT_EQ(a <=> "abc", std::weak_ordering::equivalent);
}

TEST(SString_Comparison, EmptyLessThanNonEmpty)
{
    m::sstring a;
    m::sstring b("a"sv);
    EXPECT_LT(a, b);
}

// ============================================================================
// case_insensitive_less
// ============================================================================

TEST(SString_CaseInsensitiveLess, LowerLessThanUpper)
{
    auto cmp = m::case_insensitive_less<m::sstring>{};
    m::sstring a("apple"sv);
    m::sstring b("BANANA"sv);
    // "apple" < "banana" case-insensitively
    EXPECT_TRUE(cmp(a, b));
    EXPECT_FALSE(cmp(b, a));
}

TEST(SString_CaseInsensitiveLess, SameCaseInsensitivelyIsNotLess)
{
    auto cmp = m::case_insensitive_less<m::sstring>{};
    m::sstring a("Hello"sv);
    m::sstring b("hElLo"sv);
    EXPECT_FALSE(cmp(a, b));
    EXPECT_FALSE(cmp(b, a));
}

// ============================================================================
// Substrings share the backing store — no extra allocation
// ============================================================================

TEST(SString_Sharing, SubstrSharesBackingStore)
{
    // Two substrings of the same sstring should point into the same underlying allocation.
    m::sstring s("hello world"sv);
    auto       left  = s.left(5);   // "hello"
    auto       right = s.right(5);  // "world"

    // Both views should lie within the same memory block as the original view.
    auto const base_begin = reinterpret_cast<uintptr_t>(s.view().data());
    auto const base_end   = base_begin + s.view().size();

    EXPECT_GE(reinterpret_cast<uintptr_t>(left.view().data()), base_begin);
    EXPECT_LE(reinterpret_cast<uintptr_t>(left.view().data()), base_end);

    EXPECT_GE(reinterpret_cast<uintptr_t>(right.view().data()), base_begin);
    EXPECT_LE(reinterpret_cast<uintptr_t>(right.view().data()), base_end);
}

// ============================================================================
// Thread safety — concurrent c_str() calls on a substring that needs
// to allocate a copy must not race or double-free.
// ============================================================================

TEST(SString_Threaded, ConcurrentCStr)
{
    // Create a substring that is not at the end of the backing store, so that
    // c_str() must allocate a new null-terminated buffer.
    m::sstring source("hello world"sv);
    m::sstring sub = source.left(5); // "hello" — not a suffix

    std::vector<std::thread> threads;
    threads.reserve(8);
    for (int i = 0; i < 8; ++i)
    {
        threads.emplace_back(
            [&sub]()
            {
                // All threads race to call c_str() on the same object.
                // The result must always be a valid null-terminated "hello".
                auto const cs = sub.c_str();
                EXPECT_EQ(std::string_view(cs), "hello"sv);
            });
    }

    for (auto& t : threads)
        t.join();
}

TEST(SString_Threaded, ConcurrentCopy)
{
    m::sstring shared("shared"sv);
    std::vector<std::thread> threads;
    threads.reserve(8);
    for (int i = 0; i < 8; ++i)
    {
        threads.emplace_back(
            [&shared]()
            {
                auto local = shared;
                EXPECT_EQ(local.view(), "shared"sv);
            });
    }
    for (auto& t : threads)
        t.join();
    EXPECT_EQ(shared.view(), "shared"sv);
}
