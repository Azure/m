// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <chrono>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <thread>

#include <m/test_data/test_data.h>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

auto test_strings = {"Alfa"s,   "Bravo"s,    "Charlie"s, "Delta"s,   "Echo"s,    "Foxtrot"s,
                     "Golf"s,   "Hotel"s,    "India"s,   "Juliett"s, "Kilo"s,    "Lima"s,
                     "Mike"s,   "November"s, "Oscar"s,   "Papa"s,    "Quebec"s,  "Romeo"s,
                     "Sierra"s, "Tango"s,    "Uniform"s, "Victor"s,  "Whiskey"s, "Xray"s,
                     "Yankee"s, "Zulu"s};

TEST(TestNatoStrings, VerifyBasicStringCharInitList)
{
    auto it1 = test_strings.begin();
    auto it2 = m::test_data::nato_alphabet_s.begin();

    for (std::size_t i = 0; i < 26; i++)
    {
        auto s1 = *it1++;
        auto s2 = *it2++;
        EXPECT_EQ(s1, s2);
    }
}
