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

#include <m/chonk/chonk.h>

using namespace std::chrono_literals;
using namespace std::string_literals;

struct SomeStruct
{
    int x;
    int y;
};

TEST(Chonk, SimpleEmptyChonk)
{
    m::chonk<SomeStruct, 10> mychonk;

    EXPECT_EQ(0, mychonk.size());
    EXPECT_EQ(10, mychonk.capacity());
}

TEST(Chonk, ChonkOfInts)
{
    m::chonk<int, 32> someints;

    someints.assign(10, 42);

    EXPECT_EQ(someints.size(), 10);
    EXPECT_EQ(someints[0], 42);
    EXPECT_EQ(someints[1], 42);
    EXPECT_EQ(someints[2], 42);
}

TEST(Chonk, ChonkOfStrings)
{
    m::chonk<std::string, 32> ch;

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
