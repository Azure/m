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
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#endif
    clock::time_point pt(clock::now());
#ifdef __clang__
#pragma clang diagnostic pop
#endif
}
