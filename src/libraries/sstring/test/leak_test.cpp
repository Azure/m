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

#include <m/sstring/sstring.h>
#include <m/strings/compare.h>
#include <m/test_data/test_data.h>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(TestSString, LeakTest)
{
    //
    // Start by forming a big wstring
    //

    std::wstring ws = L"1234567890"s;

    for (std::size_t i = 0; i < 12; i++)
        ws = ws + ws;

    for (std::size_t i = 0; i < 1000; i++)
    {
        auto t = m::wsstring(ws);
    }
}
