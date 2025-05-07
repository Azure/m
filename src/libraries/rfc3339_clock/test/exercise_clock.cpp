// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <m/rfc3339_clock/rfc3339_clock.h>

TEST(ExerciseClock, first)
{ 
    using clock = m::rfc3339_clock;

    // Tests nothing except that this compiles.
    //
    clock::time_point pt(clock::now());
}
