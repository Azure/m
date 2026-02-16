// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>

#include <m/math/math.h>

//
// Comprehensive tests for signed + signed -> signed operations
// These tests verify the fixes for issue #1 in CHECKLIST.md
//

TEST(SignedSignedArithmetic, AdditionBasicCases)
{
    // Basic positive addition
    EXPECT_EQ(m::math::add(int32_t{5}, int32_t{10}, int32_t{}), 15);
    EXPECT_EQ(m::math::add(int32_t{100}, int32_t{200}, int32_t{}), 300);

    // Adding zero
    EXPECT_EQ(m::math::add(int32_t{42}, int32_t{0}, int32_t{}), 42);
    EXPECT_EQ(m::math::add(int32_t{0}, int32_t{42}, int32_t{}), 42);
    EXPECT_EQ(m::math::add(int32_t{0}, int32_t{0}, int32_t{}), 0);

    // Negative addition
    EXPECT_EQ(m::math::add(int32_t{-5}, int32_t{-10}, int32_t{}), -15);
    EXPECT_EQ(m::math::add(int32_t{-100}, int32_t{-200}, int32_t{}), -300);

    // Mixed signs (should not overflow for reasonable values)
    EXPECT_EQ(m::math::add(int32_t{100}, int32_t{-50}, int32_t{}), 50);
    EXPECT_EQ(m::math::add(int32_t{-100}, int32_t{50}, int32_t{}), -50);
    EXPECT_EQ(m::math::add(int32_t{100}, int32_t{-100}, int32_t{}), 0);
}

TEST(SignedSignedArithmetic, AdditionPositiveOverflow)
{
    constexpr auto max32 = (std::numeric_limits<int32_t>::max)();

    // max + 1 should overflow
    EXPECT_THROW(m::math::add(max32, int32_t{1}, int32_t{}), std::overflow_error);

    // max + positive should overflow
    EXPECT_THROW(m::math::add(max32, int32_t{100}, int32_t{}), std::overflow_error);

    // Large positive + large positive should overflow
    EXPECT_THROW(m::math::add(max32 - 10, int32_t{20}, int32_t{}), std::overflow_error);

    // max + max should definitely overflow
    EXPECT_THROW(m::math::add(max32, max32, int32_t{}), std::overflow_error);

    // Edge case: max + 0 should NOT overflow
    EXPECT_EQ(m::math::add(max32, int32_t{0}, int32_t{}), max32);

    // Edge case: (max - 1) + 1 should NOT overflow
    EXPECT_EQ(m::math::add(max32 - 1, int32_t{1}, int32_t{}), max32);
}

TEST(SignedSignedArithmetic, AdditionNegativeOverflow)
{
    constexpr auto min32 = (std::numeric_limits<int32_t>::min)();

    // min + (-1) should overflow
    EXPECT_THROW(m::math::add(min32, int32_t{-1}, int32_t{}), std::overflow_error);

    // min + negative should overflow
    EXPECT_THROW(m::math::add(min32, int32_t{-100}, int32_t{}), std::overflow_error);

    // Large negative + large negative should overflow
    EXPECT_THROW(m::math::add(min32 + 10, int32_t{-20}, int32_t{}), std::overflow_error);

    // min + min should definitely overflow
    EXPECT_THROW(m::math::add(min32, min32, int32_t{}), std::overflow_error);

    // Edge case: min + 0 should NOT overflow
    EXPECT_EQ(m::math::add(min32, int32_t{0}, int32_t{}), min32);

    // Edge case: (min + 1) + (-1) should NOT overflow
    EXPECT_EQ(m::math::add(min32 + 1, int32_t{-1}, int32_t{}), min32);
}

TEST(SignedSignedArithmetic, AdditionMixedSignsNoOverflow)
{
    constexpr auto max32 = (std::numeric_limits<int32_t>::max)();
    constexpr auto min32 = (std::numeric_limits<int32_t>::min)();

    // max + negative should not overflow if result is in range
    EXPECT_EQ(m::math::add(max32, int32_t{-1}, int32_t{}), max32 - 1);
    EXPECT_EQ(m::math::add(max32, int32_t{-100}, int32_t{}), max32 - 100);

    // min + positive should not overflow if result is in range
    EXPECT_EQ(m::math::add(min32, int32_t{1}, int32_t{}), min32 + 1);
    EXPECT_EQ(m::math::add(min32, int32_t{100}, int32_t{}), min32 + 100);

    // Cancellation
    EXPECT_EQ(m::math::add(max32, min32, int32_t{}), -1);
}

TEST(SignedSignedArithmetic, SubtractionBasicCases)
{
    // Basic positive subtraction
    EXPECT_EQ(m::math::subtract(int32_t{10}, int32_t{5}, int32_t{}), 5);
    EXPECT_EQ(m::math::subtract(int32_t{200}, int32_t{100}, int32_t{}), 100);

    // Subtracting zero
    EXPECT_EQ(m::math::subtract(int32_t{42}, int32_t{0}, int32_t{}), 42);
    EXPECT_EQ(m::math::subtract(int32_t{0}, int32_t{0}, int32_t{}), 0);

    // Subtracting from zero (negation)
    EXPECT_EQ(m::math::subtract(int32_t{0}, int32_t{42}, int32_t{}), -42);

    // Negative subtraction
    EXPECT_EQ(m::math::subtract(int32_t{-5}, int32_t{-10}, int32_t{}), 5);
    EXPECT_EQ(m::math::subtract(int32_t{-10}, int32_t{-5}, int32_t{}), -5);

    // Mixed signs
    EXPECT_EQ(m::math::subtract(int32_t{100}, int32_t{-50}, int32_t{}), 150);
    EXPECT_EQ(m::math::subtract(int32_t{-100}, int32_t{50}, int32_t{}), -150);

    // Result equals zero
    EXPECT_EQ(m::math::subtract(int32_t{42}, int32_t{42}, int32_t{}), 0);
}

TEST(SignedSignedArithmetic, SubtractionPositiveOverflow)
{
    constexpr auto max32 = (std::numeric_limits<int32_t>::max)();
    constexpr auto min32 = (std::numeric_limits<int32_t>::min)();

    // Subtracting negative from positive can overflow
    // max - (-1) = max + 1 should overflow
    EXPECT_THROW(m::math::subtract(max32, int32_t{-1}, int32_t{}), std::overflow_error);

    // max - (-100) should overflow
    EXPECT_THROW(m::math::subtract(max32, int32_t{-100}, int32_t{}), std::overflow_error);

    // Large positive - large negative should overflow
    EXPECT_THROW(m::math::subtract(max32 - 10, int32_t{-20}, int32_t{}), std::overflow_error);

    // max - min should overflow (this is the classic edge case)
    EXPECT_THROW(m::math::subtract(max32, min32, int32_t{}), std::overflow_error);

    // Edge case: max - 0 should NOT overflow
    EXPECT_EQ(m::math::subtract(max32, int32_t{0}, int32_t{}), max32);

    // Edge case: max - 1 should NOT overflow
    EXPECT_EQ(m::math::subtract(max32, int32_t{1}, int32_t{}), max32 - 1);

    // Edge case: (max - 1) - (-1) should NOT overflow
    EXPECT_EQ(m::math::subtract(max32 - 1, int32_t{-1}, int32_t{}), max32);
}

TEST(SignedSignedArithmetic, SubtractionNegativeOverflow)
{
    constexpr auto max32 = (std::numeric_limits<int32_t>::max)();
    constexpr auto min32 = (std::numeric_limits<int32_t>::min)();

    // Subtracting positive from negative can overflow
    // min - 1 should overflow
    EXPECT_THROW(m::math::subtract(min32, int32_t{1}, int32_t{}), std::overflow_error);

    // min - 100 should overflow
    EXPECT_THROW(m::math::subtract(min32, int32_t{100}, int32_t{}), std::overflow_error);

    // Large negative - large positive should overflow
    EXPECT_THROW(m::math::subtract(min32 + 10, int32_t{20}, int32_t{}), std::overflow_error);

    // min - max should overflow
    EXPECT_THROW(m::math::subtract(min32, max32, int32_t{}), std::overflow_error);

    // Edge case: min - 0 should NOT overflow
    EXPECT_EQ(m::math::subtract(min32, int32_t{0}, int32_t{}), min32);

    // Edge case: min - (-1) should NOT overflow
    EXPECT_EQ(m::math::subtract(min32, int32_t{-1}, int32_t{}), min32 + 1);

    // Edge case: (min + 1) - 1 should NOT overflow
    EXPECT_EQ(m::math::subtract(min32 + 1, int32_t{1}, int32_t{}), min32);
}

TEST(SignedSignedArithmetic, SubtractionSameSignNoOverflow)
{
    constexpr auto max32 = (std::numeric_limits<int32_t>::max)();
    constexpr auto min32 = (std::numeric_limits<int32_t>::min)();

    // Positive - positive generally safe (unless result goes too negative)
    EXPECT_EQ(m::math::subtract(int32_t{100}, int32_t{50}, int32_t{}), 50);
    EXPECT_EQ(m::math::subtract(int32_t{50}, int32_t{100}, int32_t{}), -50);

    // max - max = 0
    EXPECT_EQ(m::math::subtract(max32, max32, int32_t{}), 0);

    // Negative - negative generally safe
    EXPECT_EQ(m::math::subtract(int32_t{-100}, int32_t{-50}, int32_t{}), -50);
    EXPECT_EQ(m::math::subtract(int32_t{-50}, int32_t{-100}, int32_t{}), 50);

    // min - min = 0
    EXPECT_EQ(m::math::subtract(min32, min32, int32_t{}), 0);
}

TEST(SignedSignedArithmetic, DifferentSizedTypes)
{
    // int8_t + int8_t -> int32_t (should always fit)
    EXPECT_EQ(m::math::add(int8_t{100}, int8_t{27}, int32_t{}), 127);
    EXPECT_EQ(m::math::add(int8_t{-100}, int8_t{-28}, int32_t{}), -128);

    // int8_t values that would overflow int8_t but fit in int32_t
    constexpr auto max8 = (std::numeric_limits<int8_t>::max)();
    constexpr auto min8 = (std::numeric_limits<int8_t>::min)();

    EXPECT_EQ(m::math::add(max8, int8_t{1}, int32_t{}), 128);
    EXPECT_EQ(m::math::add(min8, int8_t{-1}, int32_t{}), -129);

    // But should still overflow if result doesn't fit in int8_t
    EXPECT_THROW(m::math::add(max8, int8_t{1}, int8_t{}), std::overflow_error);
    EXPECT_THROW(m::math::add(min8, int8_t{-1}, int8_t{}), std::overflow_error);
}

TEST(SignedSignedArithmetic, NarrowingResults)
{
    // Results that fit in narrower type
    EXPECT_EQ(m::math::add(int32_t{50}, int32_t{50}, int8_t{}), 100);
    EXPECT_EQ(m::math::subtract(int32_t{50}, int32_t{150}, int8_t{}), -100);

    // Results that don't fit in narrower type (should throw from try_cast)
    EXPECT_THROW(m::math::add(int32_t{100}, int32_t{100}, int8_t{}), std::overflow_error);
    EXPECT_THROW(m::math::subtract(int32_t{-100}, int32_t{100}, int8_t{}), std::overflow_error);
}
