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

#include <m/error_handling/macros.h>

using namespace std::chrono_literals;

TEST(ErrorHandling, First)
{
    EXPECT_EQ(1, 1);
}

