// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include <mc/queue.h>

#include <m/print/print.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(McQueue, QueueStrings)
{
    mc::queue<std::string> q;

    q.push("Hi");
    q.push("there");

    EXPECT_EQ(q.pop(), "Hi");
    EXPECT_EQ(q.pop(), "there");
    EXPECT_EQ(q.pop().has_value(), false);
}

TEST(McQueue, QueueTest2)
{
    std::array<std::string, 3> strings{"first"s, "second"s, "third"s};

    size_t i{};

    auto lamb = [&](std::string const& s) {
        if (i < strings.size())
        {
            EXPECT_EQ(s, strings[i++]);
        }
    };

    mc::queue<std::string> q;

    for (auto&& e: strings)
        q.push(e);

    EXPECT_EQ(q.pop(lamb), true);
    EXPECT_EQ(q.pop(lamb), true);
    EXPECT_EQ(q.pop(lamb), true);
    EXPECT_EQ(q.pop(lamb), false);
}
