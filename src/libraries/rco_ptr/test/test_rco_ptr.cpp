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

#include <m/rco_ptr/rco_ptr.h>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

auto test_strings = {"Alfa"s,   "Bravo"s,    "Charlie"s, "Delta"s,  "Echo"s,    "Foxtrot"s,
                     "Golf"s,   "Hotel"s,    "India"s,   "Juliet"s, "Kilo"s,    "Lima"s,
                     "Mike"s,   "November"s, "Oscar"s,   "Papa"s,   "Quebec"s,  "Romeo"s,
                     "Sierra"s, "Tango"s,    "Uniform"s, "Victor"s, "Whiskey"s, "X-Ray"s,
                     "Yankee"s, "Zulu"s};

TEST(TestRefCount, First)
{
    auto p = m::make_rco<std::string>("Hello there");

    //

}

