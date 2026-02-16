// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>

#include <m/math/math.h>

//
// Comprehensive tests for signed + unsigned -> unsigned operations
// These tests verify the fixes for issue #2 in CHECKLIST.md
//

TEST(SignedUnsignedToUnsigned, AdditionBasicCases)
{
    // Positive signed + unsigned
    EXPECT_EQ(m::math::add(int32_t{5}, uint32_t{10}, uint32_t{}), 15u);
    EXPECT_EQ(m::math::add(int32_t{100}, uint32_t{200}, uint32_t{}), 300u);
    
    // Zero cases
    EXPECT_EQ(m::math::add(int32_t{0}, uint32_t{42}, uint32_t{}), 42u);
    EXPECT_EQ(m::math::add(int32_t{42}, uint32_t{0}, uint32_t{}), 42u);
    EXPECT_EQ(m::math::add(int32_t{0}, uint32_t{0}, uint32_t{}), 0u);
}

TEST(SignedUnsignedToUnsigned, AdditionNegativeSigned)
{
    // Negative signed + unsigned where result is positive
    EXPECT_EQ(m::math::add(int32_t{-5}, uint32_t{10}, uint32_t{}), 5u);
    EXPECT_EQ(m::math::add(int32_t{-100}, uint32_t{200}, uint32_t{}), 100u);
    
    // Negative signed + unsigned where result is exactly zero
    EXPECT_EQ(m::math::add(int32_t{-10}, uint32_t{10}, uint32_t{}), 0u);
    EXPECT_EQ(m::math::add(int32_t{-100}, uint32_t{100}, uint32_t{}), 0u);
}

TEST(SignedUnsignedToUnsigned, AdditionNegativeResult)
{
    // Negative signed + unsigned where result would be negative (should throw)
    EXPECT_THROW(m::math::add(int32_t{-10}, uint32_t{5}, uint32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::add(int32_t{-100}, uint32_t{50}, uint32_t{}), std::overflow_error);
    
    // Edge case: negative signed + 0
    EXPECT_THROW(m::math::add(int32_t{-1}, uint32_t{0}, uint32_t{}), std::overflow_error);
}

TEST(SignedUnsignedToUnsigned, AdditionMostNegativeValue)
{
    constexpr auto min32 = (std::numeric_limits<int32_t>::min)();
    
    // INT_MIN + 0 should throw (negative result)
    EXPECT_THROW(m::math::add(min32, uint32_t{0}, uint32_t{}), std::overflow_error);
    
    // INT_MIN + small value should throw (still negative)
    EXPECT_THROW(m::math::add(min32, uint32_t{100}, uint32_t{}), std::overflow_error);
    
    // INT_MIN + |INT_MIN| should equal 0
    // |INT_MIN| as uint32_t is 2^31 = 2147483648
    constexpr uint32_t abs_min = static_cast<uint32_t>(-(static_cast<int64_t>(min32)));
    EXPECT_EQ(m::math::add(min32, abs_min, uint32_t{}), 0u);
    
    // INT_MIN + (|INT_MIN| + 1) should equal 1
    EXPECT_EQ(m::math::add(min32, abs_min + 1, uint32_t{}), 1u);
    
    // INT_MIN + (|INT_MIN| - 1) should throw (result = -1)
    EXPECT_THROW(m::math::add(min32, abs_min - 1, uint32_t{}), std::overflow_error);
}

TEST(SignedUnsignedToUnsigned, AdditionPositiveOverflow)
{
    constexpr auto max32_signed = (std::numeric_limits<int32_t>::max)();
    constexpr auto max32_unsigned = (std::numeric_limits<uint32_t>::max)();
    
    // Large positive signed + large unsigned should overflow uint32_t
    EXPECT_THROW(m::math::add(max32_signed, max32_unsigned, uint32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::add(max32_signed, max32_unsigned / 2, uint32_t{}), std::overflow_error);
    
    // Edge case: values that just fit
    EXPECT_EQ(m::math::add(int32_t{1}, max32_unsigned - 1, uint32_t{}), max32_unsigned);
}

TEST(SignedUnsignedToUnsigned, SubtractionBasicCases)
{
    // Positive signed - unsigned (result positive)
    EXPECT_EQ(m::math::subtract(int32_t{10}, uint32_t{5}, uint32_t{}), 5u);
    EXPECT_EQ(m::math::subtract(int32_t{200}, uint32_t{100}, uint32_t{}), 100u);
    
    // Subtract zero
    EXPECT_EQ(m::math::subtract(int32_t{42}, uint32_t{0}, uint32_t{}), 42u);
    EXPECT_EQ(m::math::subtract(int32_t{0}, uint32_t{0}, uint32_t{}), 0u);
    
    // Result equals zero
    EXPECT_EQ(m::math::subtract(int32_t{42}, uint32_t{42}, uint32_t{}), 0u);
}

TEST(SignedUnsignedToUnsigned, SubtractionNegativeSigned)
{
    // Any negative signed - unsigned must be negative (should throw)
    EXPECT_THROW(m::math::subtract(int32_t{-1}, uint32_t{0}, uint32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::subtract(int32_t{-10}, uint32_t{5}, uint32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::subtract(int32_t{-100}, uint32_t{50}, uint32_t{}), std::overflow_error);
    
    constexpr auto min32 = (std::numeric_limits<int32_t>::min)();
    EXPECT_THROW(m::math::subtract(min32, uint32_t{0}, uint32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::subtract(min32, uint32_t{100}, uint32_t{}), std::overflow_error);
}

TEST(SignedUnsignedToUnsigned, SubtractionNegativeResult)
{
    // Positive signed - larger unsigned (result negative, should throw)
    EXPECT_THROW(m::math::subtract(int32_t{5}, uint32_t{10}, uint32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::subtract(int32_t{100}, uint32_t{200}, uint32_t{}), std::overflow_error);
    
    // Zero - unsigned (negative result)
    EXPECT_THROW(m::math::subtract(int32_t{0}, uint32_t{1}, uint32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::subtract(int32_t{0}, uint32_t{100}, uint32_t{}), std::overflow_error);
}

TEST(SignedUnsignedToUnsigned, SubtractionEdgeCases)
{
    constexpr auto max32_signed = (std::numeric_limits<int32_t>::max)();
    constexpr auto max32_unsigned = (std::numeric_limits<uint32_t>::max)();
    
    // INT_MAX - UINT_MAX should throw (negative result)
    EXPECT_THROW(m::math::subtract(max32_signed, max32_unsigned, uint32_t{}), std::overflow_error);
    
    // INT_MAX - (INT_MAX - 1) should equal 1
    EXPECT_EQ(m::math::subtract(max32_signed, static_cast<uint32_t>(max32_signed) - 1, uint32_t{}), 1u);
    
    // INT_MAX - INT_MAX should equal 0
    EXPECT_EQ(m::math::subtract(max32_signed, static_cast<uint32_t>(max32_signed), uint32_t{}), 0u);
    
    // INT_MAX - (INT_MAX + 1) should throw
    EXPECT_THROW(m::math::subtract(max32_signed, static_cast<uint32_t>(max32_signed) + 1, uint32_t{}), std::overflow_error);
}

TEST(SignedUnsignedToUnsigned, DifferentSizedTypes)
{
    // int8_t + uint8_t -> uint32_t (should always fit)
    EXPECT_EQ(m::math::add(int8_t{100}, uint8_t{27}, uint32_t{}), 127u);
    EXPECT_EQ(m::math::add(int8_t{-50}, uint8_t{100}, uint32_t{}), 50u);
    
    // int8_t + uint8_t -> uint8_t (may overflow)
    EXPECT_EQ(m::math::add(int8_t{100}, uint8_t{100}, uint8_t{}), 200u);
    EXPECT_THROW(m::math::add(int8_t{100}, uint8_t{200}, uint8_t{}), std::overflow_error);
    
    // Negative int8_t
    EXPECT_THROW(m::math::add(int8_t{-1}, uint8_t{0}, uint8_t{}), std::overflow_error);
    EXPECT_EQ(m::math::add(int8_t{-10}, uint8_t{20}, uint8_t{}), 10u);
}

TEST(SignedUnsignedToUnsigned, NarrowingResults)
{
    // Results that fit in narrower type
    EXPECT_EQ(m::math::add(int32_t{50}, uint32_t{50}, uint8_t{}), 100u);
    EXPECT_EQ(m::math::subtract(int32_t{150}, uint32_t{50}, uint8_t{}), 100u);
    
    // Results that don't fit in narrower type (should throw from try_cast)
    EXPECT_THROW(m::math::add(int32_t{200}, uint32_t{200}, uint8_t{}), std::overflow_error);
    EXPECT_THROW(m::math::subtract(int32_t{1000}, uint32_t{500}, uint8_t{}), std::overflow_error);
}
