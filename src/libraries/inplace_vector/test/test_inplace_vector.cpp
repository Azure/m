// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include <m/inplace_vector/inplace_vector.h>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

namespace
{
    // A type with only operator< and operator== (no operator<=>); used to exercise
    // the synthesized weak_ordering path of inplace_vector's operator<=>.
    struct less_only
    {
        int m_v;

        friend constexpr bool
        operator<(less_only const& a, less_only const& b)
        {
            return a.m_v < b.m_v;
        }

        friend constexpr bool
        operator==(less_only const& a, less_only const& b)
        {
            return a.m_v == b.m_v;
        }
    };

    // A sized range that reports a dishonestly huge size() while iterating as empty.
    // Used to drive inplace_vector::append_range's integer-overflow guard: the size
    // check (size() + ranges::size(rnge)) wraps SIZE_MAX before any iteration happens.
    struct lying_huge_range
    {
        static int const*
        begin()
        {
            return nullptr;
        }

        static int const*
        end()
        {
            return nullptr;
        }

        static std::size_t
        size()
        {
            return std::numeric_limits<std::size_t>::max();
        }
    };
} // namespace

struct SomeStruct
{
    int x;
    int y;
};

struct foo
{
    foo() { ms_default_constructions++; }

    foo(std::string s): m_s(std::move(s)) { ms_string_constructions++; }

    foo(foo const& other)
    {
        m_s = other.m_s;
        ms_copy_constructions++;
    }

    foo(foo&& other) noexcept
    {
        using std::swap;
        swap(m_s, other.m_s);
        ms_move_constructions++;
    }

    foo&
    operator=(foo const& other)
    {
        ms_copy_assignments++;
        m_s = other.m_s;
        return *this;
    }

    foo&
    operator=(foo&& other) noexcept
    {
        ms_move_assignments++;

        using std::swap;
        swap(m_s, other.m_s);

        return *this;
    }

    void
    swap(foo& other) noexcept
    {
        ms_swaps++;

        using std::swap;

        swap(m_s, other.m_s);
    }

    ~foo() { ms_destructions++; }

    bool
    operator==(std::string const& str) const
    {
        return m_s == str;
    }

    std::string m_s;

    static inline std::atomic<intmax_t> ms_default_constructions;
    static inline std::atomic<intmax_t> ms_string_constructions;
    static inline std::atomic<intmax_t> ms_copy_constructions;
    static inline std::atomic<intmax_t> ms_move_constructions;
    static inline std::atomic<intmax_t> ms_copy_assignments;
    static inline std::atomic<intmax_t> ms_move_assignments;
    static inline std::atomic<intmax_t> ms_swaps;
    static inline std::atomic<intmax_t> ms_destructions;
};

struct foo_stats
{
    static foo_stats
    get()
    {
        foo_stats r;

        r.m_default_constructions = foo::ms_default_constructions;
        r.m_string_constructions  = foo::ms_string_constructions;
        r.m_copy_constructions    = foo::ms_copy_constructions;
        r.m_move_constructions    = foo::ms_move_constructions;
        r.m_copy_assignments      = foo::ms_copy_assignments;
        r.m_move_assignments      = foo::ms_move_assignments;
        r.m_swaps                 = foo::ms_swaps;
        r.m_destructions          = foo::ms_destructions;

        return r;
    }

    friend foo_stats
    operator-(foo_stats const& l, foo_stats const& r)
    {
        foo_stats x;

        x.m_default_constructions = l.m_default_constructions - r.m_default_constructions;
        x.m_string_constructions  = l.m_string_constructions - r.m_string_constructions;
        x.m_copy_constructions    = l.m_copy_constructions - r.m_copy_constructions;
        x.m_move_constructions    = l.m_move_constructions - r.m_move_constructions;
        x.m_copy_assignments      = l.m_copy_assignments - r.m_copy_assignments;
        x.m_move_assignments      = l.m_move_assignments - r.m_move_assignments;
        x.m_swaps                 = l.m_swaps - r.m_swaps;
        x.m_destructions          = l.m_destructions - r.m_destructions;

        return x;
    }

    intmax_t m_default_constructions;
    intmax_t m_string_constructions;
    intmax_t m_copy_constructions;
    intmax_t m_move_constructions;
    intmax_t m_copy_assignments;
    intmax_t m_move_assignments;
    intmax_t m_swaps;
    intmax_t m_destructions;
};

auto test_strings = {"Alfa"s,   "Bravo"s,    "Charlie"s, "Delta"s,  "Echo"s,    "Foxtrot"s,
                     "Golf"s,   "Hotel"s,    "India"s,   "Juliet"s, "Kilo"s,    "Lima"s,
                     "Mike"s,   "November"s, "Oscar"s,   "Papa"s,   "Quebec"s,  "Romeo"s,
                     "Sierra"s, "Tango"s,    "Uniform"s, "Victor"s, "Whiskey"s, "X-Ray"s,
                     "Yankee"s, "Zulu"s};

TEST(InplaceVector, SimpleEmptyInplaceVector)
{
    m::inplace_vector<SomeStruct, 10> myinplace_vector;

    EXPECT_EQ(0, myinplace_vector.size());
    EXPECT_EQ(10, myinplace_vector.capacity());
}

TEST(InplaceVector, InplaceVectorOfInts)
{
    m::inplace_vector<int, 32> someints;

    someints.assign(10, 42);

    EXPECT_EQ(someints.size(), 10);
    EXPECT_EQ(someints[0], 42);
    EXPECT_EQ(someints[1], 42);
    EXPECT_EQ(someints[2], 42);
}

TEST(InplaceVector, Erase1)
{
    m::inplace_vector<std::string, 32> ch;

    ch.assign({"hello"s, "there"s, "my"s, "friends"s});

    EXPECT_EQ(ch.size(), 4);

    EXPECT_EQ(ch[0], "hello"s);
    EXPECT_EQ(ch[1], "there"s);
    EXPECT_EQ(ch[2], "my"s);
    EXPECT_EQ(ch[3], "friends"s);

    auto it = ch.begin();

    it++;

    ch.erase(it);

    EXPECT_EQ(ch[1], "my"s);
    EXPECT_EQ(ch.size(), 3);
}

TEST(InplaceVector, EraseIterator)
{
    m::inplace_vector<std::string, 32> ch;

    for (auto&& e: test_strings)
        ch.push_back(e);

    EXPECT_EQ(ch.size(), 26);

    auto it = ch.begin();

    it += 3;

    auto it2 = it;

    it2 += 2;

    EXPECT_EQ(*it, "Delta"s);
    EXPECT_EQ(*it2, "Foxtrot"s);

    auto it3 = ch.erase(it, it2);

    EXPECT_EQ(ch.size(), 24);
    EXPECT_EQ(ch[4], "Golf"s);
    EXPECT_EQ(ch[23], "Zulu"s);

    EXPECT_NE(it3, ch.end());
}

TEST(InplaceVector, CountMoves)
{
    m::inplace_vector<foo, 100> ch;

    auto const s1 = foo_stats::get();

    for (auto&& e: test_strings)
        ch.push_back(e);

    auto const s2    = foo_stats::get();
    auto const diff1 = s2 - s1;

    EXPECT_EQ(diff1.m_string_constructions, 26);

    EXPECT_EQ(ch[8].m_s, "India"s);
}

TEST(InplaceVector, ThreeWayEqual)
{
    m::inplace_vector<int, 8> a;
    m::inplace_vector<int, 8> b;

    a.assign({1, 2, 3});
    b.assign({1, 2, 3});

    EXPECT_TRUE((a <=> b) == std::strong_ordering::equal);
    EXPECT_TRUE(a == b);
}

TEST(InplaceVector, ThreeWayLessByElement)
{
    m::inplace_vector<int, 8> a;
    m::inplace_vector<int, 8> b;

    a.assign({1, 2, 3});
    b.assign({1, 9, 3});

    // Same size, differ at the second element: a < b.
    EXPECT_TRUE((a <=> b) == std::strong_ordering::less);
    EXPECT_TRUE(a < b);
    EXPECT_TRUE(b > a);
    EXPECT_FALSE(a == b);
}

TEST(InplaceVector, ThreeWayGreaterByElement)
{
    m::inplace_vector<int, 8> a;
    m::inplace_vector<int, 8> b;

    a.assign({1, 9, 0});
    b.assign({1, 2, 9});

    // First difference is at index 1 where 9 > 2, so a > b regardless of later elements.
    EXPECT_TRUE((a <=> b) == std::strong_ordering::greater);
    EXPECT_TRUE(a > b);
}

TEST(InplaceVector, ThreeWayPrefixIsLess)
{
    m::inplace_vector<int, 8> a;
    m::inplace_vector<int, 8> b;

    a.assign({1, 2});
    b.assign({1, 2, 3});

    // A proper prefix orders before the longer sequence.
    EXPECT_TRUE((a <=> b) == std::strong_ordering::less);
    EXPECT_TRUE(a < b);
    EXPECT_TRUE(b > a);
}

TEST(InplaceVector, ThreeWayEmptyOrdering)
{
    m::inplace_vector<int, 8> empty;
    m::inplace_vector<int, 8> nonempty;

    nonempty.assign({0});

    EXPECT_TRUE((empty <=> empty) == std::strong_ordering::equal);
    EXPECT_TRUE((empty <=> nonempty) == std::strong_ordering::less);
    EXPECT_TRUE(empty < nonempty);
}

TEST(InplaceVector, ThreeWaySynthFromLessOnly)
{
    // A type with only operator< and operator== (no operator<=>) must still order
    // via the synthesized weak_ordering path.
    m::inplace_vector<less_only, 8> a;
    m::inplace_vector<less_only, 8> b;

    a.push_back(less_only{1});
    a.push_back(less_only{2});
    b.push_back(less_only{1});
    b.push_back(less_only{5});

    EXPECT_TRUE((a <=> b) == std::weak_ordering::less);
    EXPECT_TRUE(a < b);
}

TEST(InplaceVector, AppendRangeIntegerOverflowThrows)
{
    // append_range checks size() + ranges::size(rnge) against capacity(). When the
    // sum overflows SIZE_MAX, m::math::add throws std::overflow_error rather than
    // wrapping and slipping past the capacity guard (the bug this check fixes).
    m::inplace_vector<int, 8> v;
    v.push_back(1); // size() == 1, so 1 + SIZE_MAX overflows.

    EXPECT_THROW(v.append_range(lying_huge_range{}), std::overflow_error);

    // The vector must be unchanged after the failed append.
    EXPECT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0], 1);
}

TEST(InplaceVector, AppendRangeOverCapacityThrows)
{
    // A range that fits in SIZE_MAX arithmetic but exceeds capacity still throws
    // bad_alloc (the ordinary capacity guard, distinct from the overflow guard).
    m::inplace_vector<int, 4> v;

    std::array<int, 5> const src{1, 2, 3, 4, 5};

    EXPECT_THROW(v.append_range(src), std::bad_alloc);
}

