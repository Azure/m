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

#include <m/inplace_vector/inplace_vector.h>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

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
