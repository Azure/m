// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>

#include <m/bitset/bitset.h>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

struct SomeStruct
{
    int x;
    int y;
};

TEST(TestBitset, CreateBitset)
{
    m::bitset<512> bits;
    std::ignore = bits;
    //
    //
}

TEST(TestBitset, FindBit)
{
    m::bitset<512> bits;

    auto x = bits.find_first_clear_and_set();
    EXPECT_TRUE(x.has_value());
}

TEST(TestBitset, Find100Bits)
{
    m::bitset<512> bits;

    std::array<std::size_t, 100> found_bits;

    for (auto&& e: found_bits)
        e = bits.find_first_clear_and_set().value();

    std::ranges::sort(found_bits);

    auto it = std::unique(found_bits.begin(), found_bits.end());

    EXPECT_EQ(it, found_bits.end());
}

TEST(TestBitset, Find126Bits)
{
    m::bitset<127> bits;

    std::array<std::size_t, 126> found_bits;

    for (auto&& e: found_bits)
        e = bits.find_first_clear_and_set().value();

    std::ranges::sort(found_bits);

    auto it = std::unique(found_bits.begin(), found_bits.end());

    EXPECT_EQ(it, found_bits.end());
}

TEST(TestBitset, SetAndTestBit)
{
    m::bitset<512> bits;

    bits.set(42);

    EXPECT_TRUE(bits.is_set(42));
    EXPECT_FALSE(bits.is_set(41));
    EXPECT_FALSE(bits.is_set(43));
}

// Additional unit tests for m::bitset

TEST(TestBitset, AllBitsInitiallyUnset)
{
    m::bitset<128> bits;
    for (std::size_t i = 0; i < bits.size(); ++i)
        EXPECT_FALSE(bits.is_set(i));
}

TEST(TestBitset, SetAndUnsetBit)
{
    m::bitset<64> bits;
    bits.set(10);
    EXPECT_TRUE(bits.is_set(10));
    bits.clear(10);
    EXPECT_FALSE(bits.is_set(10));
}

TEST(TestBitset, FindFirstUnsetAndSetExhausts)
{
    m::bitset<8> bits;
    std::array<std::size_t, bits.size()> indices;
    for (auto& idx : indices)
        idx = bits.find_first_clear_and_set().value();
    EXPECT_EQ(bits.find_first_clear_and_set(), std::nullopt);
    std::ranges::sort(indices);
    for (std::size_t i = 0; i < bits.size(); ++i)
        EXPECT_EQ(indices[i], i);
}

TEST(TestBitset, FindFirstSetAndUnsetExhausts)
{
    m::bitset<8> bits;
    // Set all bits
    for (std::size_t i = 0; i < 8; ++i)
        bits.set(i);
    std::array<std::size_t, 8> indices;
    for (auto& idx : indices)
        idx = bits.find_first_set_and_clear().value();
    EXPECT_EQ(bits.find_first_set_and_clear(), std::nullopt);
    std::ranges::sort(indices);
    for (std::size_t i = 0; i < 8; ++i)
        EXPECT_EQ(indices[i], i);
}

TEST(TestBitset, SetUnsetMultipleBits)
{
    m::bitset<16> bits;
    for (std::size_t i = 0; i < 16; i += 2)
        bits.set(i);
    for (std::size_t i = 0; i < 16; ++i)
        EXPECT_EQ(bits.is_set(i), i % 2 == 0);
    for (std::size_t i = 0; i < 16; i += 2)
        bits.clear(i);
    for (std::size_t i = 0; i < 16; ++i)
        EXPECT_FALSE(bits.is_set(i));
}

TEST(TestBitset, PopCount1)
{
    m::bitset<16> bits;

    for (std::size_t i = 0; i < 4; i++)
        bits.set(i * 2);

    EXPECT_EQ(bits.popcount(), 4);

    for (std::size_t i = 0; i < bits.size(); i++)
        bits.set(i);

    EXPECT_EQ(bits.popcount(), bits.size());
}


TEST(TestBitset, PopCount2)
{
    m::bitset<40000> bits;

    for (std::size_t i = 0; i < 2000; i++)
        bits.set(i * 3);

    EXPECT_EQ(bits.popcount(), 2000);

    for (std::size_t i = 0; i < bits.size(); i++)
        bits.set(i);

    EXPECT_EQ(bits.popcount(), bits.size());
}



