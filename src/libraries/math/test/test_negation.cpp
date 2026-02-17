// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>

#include <m/math/math.h>

//
// Comprehensive tests for negation operations
// Tests cover: signed->signed, signed->unsigned, unsigned->signed
// This file replaces the nearly empty exercise_negation.cpp
//

// ============================================================================
// Signed -> Signed Negation
// ============================================================================

TEST(NegationSignedToSigned, BasicNegation)
{
    EXPECT_EQ(m::math::negate(int32_t{42}, int32_t{}), -42);
    EXPECT_EQ(m::math::negate(int32_t{-42}, int32_t{}), 42);
    EXPECT_EQ(m::math::negate(int32_t{0}, int32_t{}), 0);
}

TEST(NegationSignedToSigned, PositiveValues)
{
    EXPECT_EQ(m::math::negate(int32_t{1}, int32_t{}), -1);
    EXPECT_EQ(m::math::negate(int32_t{100}, int32_t{}), -100);
    EXPECT_EQ(m::math::negate(int32_t{1000000}, int32_t{}), -1000000);
}

TEST(NegationSignedToSigned, NegativeValues)
{
    EXPECT_EQ(m::math::negate(int32_t{-1}, int32_t{}), 1);
    EXPECT_EQ(m::math::negate(int32_t{-100}, int32_t{}), 100);
    EXPECT_EQ(m::math::negate(int32_t{-1000000}, int32_t{}), 1000000);
}

TEST(NegationSignedToSigned, IntMinOverflow)
{
    // INT_MIN cannot be negated to INT_MAX + 1 (overflow)
    constexpr auto min32 = (std::numeric_limits<int32_t>::min)();
    EXPECT_THROW(m::math::negate(min32, int32_t{}), std::overflow_error);
}

TEST(NegationSignedToSigned, IntMaxNegation)
{
    // INT_MAX can be negated to -INT_MAX
    constexpr auto max32 = (std::numeric_limits<int32_t>::max)();
    EXPECT_EQ(m::math::negate(max32, int32_t{}), -max32);
}

// ============================================================================
// Signed -> Unsigned Negation
// ============================================================================

TEST(NegationSignedToUnsigned, NegativeToPositive)
{
    // Negative values become positive when negated to unsigned
    EXPECT_EQ(m::math::negate(int32_t{-42}, uint32_t{}), 42u);
    EXPECT_EQ(m::math::negate(int32_t{-1}, uint32_t{}), 1u);
    EXPECT_EQ(m::math::negate(int32_t{-1000}, uint32_t{}), 1000u);
}

TEST(NegationSignedToUnsigned, PositiveToNegative)
{
    // Positive values would become negative (overflow for unsigned)
    EXPECT_THROW(m::math::negate(int32_t{1}, uint32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::negate(int32_t{42}, uint32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::negate(int32_t{1000}, uint32_t{}), std::overflow_error);
}

TEST(NegationSignedToUnsigned, ZeroNegation)
{
    // Zero negated is still zero
    EXPECT_EQ(m::math::negate(int32_t{0}, uint32_t{}), 0u);
}

TEST(NegationSignedToUnsigned, IntMinSpecialCase)
{
    // INT_MIN negated = INT_MAX + 1 (fits in unsigned)
    constexpr auto min32 = (std::numeric_limits<int32_t>::min)();
    uint32_t expected = static_cast<uint32_t>(-(static_cast<int64_t>(min32)));
    EXPECT_EQ(m::math::negate(min32, uint32_t{}), expected);
}

TEST(NegationSignedToUnsigned, LargeNegativeValues)
{
    constexpr auto min32 = (std::numeric_limits<int32_t>::min)();
    
    // -INT_MAX can be negated to unsigned
    constexpr auto max32 = (std::numeric_limits<int32_t>::max)();
    EXPECT_EQ(m::math::negate(-max32, uint32_t{}), static_cast<uint32_t>(max32));
}

// ============================================================================
// Unsigned -> Signed Negation
// ============================================================================

TEST(NegationUnsignedToSigned, ZeroNegation)
{
    // Zero negated is still zero
    EXPECT_EQ(m::math::negate(uint32_t{0}, int32_t{}), 0);
}

TEST(NegationUnsignedToSigned, SmallValues)
{
    // Small unsigned values can be negated to signed
    EXPECT_EQ(m::math::negate(uint32_t{1}, int32_t{}), -1);
    EXPECT_EQ(m::math::negate(uint32_t{42}, int32_t{}), -42);
    EXPECT_EQ(m::math::negate(uint32_t{1000}, int32_t{}), -1000);
}

TEST(NegationUnsignedToSigned, MaxSignedValue)
{
    // INT_MAX as unsigned can be negated to -INT_MAX
    constexpr auto max_signed = (std::numeric_limits<int32_t>::max)();
    uint32_t max_as_unsigned = static_cast<uint32_t>(max_signed);
    EXPECT_EQ(m::math::negate(max_as_unsigned, int32_t{}), -max_signed);
}

TEST(NegationUnsignedToSigned, OverflowCases)
{
    // Values > INT_MAX cannot be negated to signed (result would be < INT_MIN)
    constexpr auto max_signed = (std::numeric_limits<int32_t>::max)();
    uint32_t too_large = static_cast<uint32_t>(max_signed) + 2;
    EXPECT_THROW(m::math::negate(too_large, int32_t{}), std::overflow_error);
    
    // UINT_MAX cannot be negated to signed
    constexpr auto max_unsigned = (std::numeric_limits<uint32_t>::max)();
    EXPECT_THROW(m::math::negate(max_unsigned, int32_t{}), std::overflow_error);
}

// ============================================================================
// Unsigned -> Unsigned Negation
// ============================================================================

TEST(NegationUnsignedToUnsigned, ZeroOnly)
{
    // Only zero can be negated to unsigned (0 -> 0)
    EXPECT_EQ(m::math::negate(uint32_t{0}, uint32_t{}), 0u);
}

TEST(NegationUnsignedToUnsigned, NonZeroOverflow)
{
    // Any non-zero unsigned value negated to unsigned should overflow
    EXPECT_THROW(m::math::negate(uint32_t{1}, uint32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::negate(uint32_t{42}, uint32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::negate(uint32_t{100}, uint32_t{}), std::overflow_error);
}

// ============================================================================
// All Integer Sizes Tests
// ============================================================================

TEST(NegationAllSizes, Int8Negation)
{
    EXPECT_EQ(m::math::negate(int8_t{42}, int8_t{}), -42);
    EXPECT_EQ(m::math::negate(int8_t{-42}, int8_t{}), 42);
    
    // INT8_MIN overflow
    constexpr auto min8 = (std::numeric_limits<int8_t>::min)();
    EXPECT_THROW(m::math::negate(min8, int8_t{}), std::overflow_error);
}

TEST(NegationAllSizes, UInt8Negation)
{
    EXPECT_EQ(m::math::negate(uint8_t{0}, uint8_t{}), 0);
    EXPECT_THROW(m::math::negate(uint8_t{1}, uint8_t{}), std::overflow_error);
    
    // Unsigned to signed
    EXPECT_EQ(m::math::negate(uint8_t{100}, int8_t{}), -100);
}

TEST(NegationAllSizes, Int16Negation)
{
    EXPECT_EQ(m::math::negate(int16_t{1000}, int16_t{}), -1000);
    EXPECT_EQ(m::math::negate(int16_t{-1000}, int16_t{}), 1000);
    
    constexpr auto min16 = (std::numeric_limits<int16_t>::min)();
    EXPECT_THROW(m::math::negate(min16, int16_t{}), std::overflow_error);
}

TEST(NegationAllSizes, UInt16Negation)
{
    EXPECT_EQ(m::math::negate(uint16_t{0}, uint16_t{}), 0);
    EXPECT_EQ(m::math::negate(uint16_t{1000}, int16_t{}), -1000);
}

TEST(NegationAllSizes, Int64Negation)
{
    EXPECT_EQ(m::math::negate(int64_t{1000000000}, int64_t{}), -1000000000LL);
    EXPECT_EQ(m::math::negate(int64_t{-1000000000}, int64_t{}), 1000000000LL);
    
    constexpr auto min64 = (std::numeric_limits<int64_t>::min)();
    EXPECT_THROW(m::math::negate(min64, int64_t{}), std::overflow_error);
}

TEST(NegationAllSizes, UInt64Negation)
{
    EXPECT_EQ(m::math::negate(uint64_t{0}, uint64_t{}), 0ULL);
    EXPECT_THROW(m::math::negate(uint64_t{1}, uint64_t{}), std::overflow_error);
}

// ============================================================================
// Mixed Size Negations (narrow to wide, wide to narrow)
// ============================================================================

TEST(NegationMixedSizes, NarrowToWide)
{
    // int8_t -> int32_t
    EXPECT_EQ(m::math::negate(int8_t{-42}, int32_t{}), 42);
    
    // int16_t -> int64_t
    EXPECT_EQ(m::math::negate(int16_t{-1000}, int64_t{}), 1000LL);
    
    // uint8_t -> int32_t
    EXPECT_EQ(m::math::negate(uint8_t{42}, int32_t{}), -42);
}

TEST(NegationMixedSizes, WideToNarrow)
{
    // int32_t -> int8_t (fits)
    EXPECT_EQ(m::math::negate(int32_t{-100}, int8_t{}), 100);
    
    // int32_t -> int8_t (overflow)
    EXPECT_THROW(m::math::negate(int32_t{-200}, int8_t{}), std::overflow_error);
    
    // int64_t -> int32_t (fits)
    EXPECT_EQ(m::math::negate(int64_t{-1000}, int32_t{}), 1000);
    
    // int64_t -> int32_t (overflow)
    constexpr auto large = 3000000000LL;
    EXPECT_THROW(m::math::negate(-large, int32_t{}), std::overflow_error);
}

// ============================================================================
// Double Negation Tests
// ============================================================================

TEST(NegationDoubleNegation, SignedSigned)
{
    // -(-x) should equal x (for values that don't overflow)
    int32_t value = 42;
    auto negated = m::math::negate(value, int32_t{});
    auto double_negated = m::math::negate(negated, int32_t{});
    EXPECT_EQ(double_negated, value);
}

TEST(NegationDoubleNegation, EdgeCases)
{
    // Zero double-negated is still zero
    EXPECT_EQ(m::math::negate(m::math::negate(int32_t{0}, int32_t{}), int32_t{}), 0);
    
    // INT_MAX double-negated
    constexpr auto max32 = (std::numeric_limits<int32_t>::max)();
    auto neg_max = m::math::negate(max32, int32_t{});
    auto double_neg = m::math::negate(neg_max, int32_t{});
    EXPECT_EQ(double_neg, max32);
}
