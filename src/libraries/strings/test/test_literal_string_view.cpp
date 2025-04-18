// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#ifndef __GNUG__

#include <m/strings/literal_string_view.h>

using namespace std::string_literals;
using namespace std::string_view_literals;
using namespace m::string_view_literals;

TEST(TestLiteralStringViews, BasicTest1)
{
    auto l1 = "test"_sl;
    EXPECT_EQ("test"sv, l1);
}

#endif
