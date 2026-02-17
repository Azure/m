// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>

#include <m/math/math.h>

//
// Comprehensive tests for subtraction operations across all type combinations
// Tests cover: unsigned-unsigned, unsigned-signed, signed-unsigned, signed-signed
// For all result types: unsigned and signed
//

// ============================================================================
// Unsigned - Unsigned -> Unsigned
// ============================================================================

TEST(SubtractionUnsignedUnsigned, BasicSubtraction)
{
    EXPECT_EQ(m::math::subtract(uint32_t{50}, uint32_t{20}, uint32_t{}), 30u);
    EXPECT_EQ(m::math::subtract(uint32_t{100}, uint32_t{100}, uint32_t{}), 0u);
    EXPECT_EQ(m::math::subtract(uint32_t{1}, uint32_t{0}, uint32_t{}), 1u);
}

TEST(SubtractionUnsignedUnsigned, SubtractFromZero)
{
    // 0 - x where x > 0 should overflow (result would be negative)
    EXPECT_THROW(m::math::subtract(uint32_t{0}, uint32_t{1}, uint32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::subtract(uint32_t{0}, uint32_t{100}, uint32_t{}), std::overflow_error);
}

TEST(SubtractionUnsignedUnsigned, NegativeResultOverflow)
{
    // l < r should overflow (result would be negative)
    EXPECT_THROW(m::math::subtract(uint32_t{10}, uint32_t{20}, uint32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::subtract(uint32_t{1}, uint32_t{2}, uint32_t{}), std::overflow_error);
}

TEST(SubtractionUnsignedUnsigned, MaxValueEdgeCases)
{
    constexpr auto max32 = (std::numeric_limits<uint32_t>::max)();
    
    // MAX - 0 = MAX
    EXPECT_EQ(m::math::subtract(max32, uint32_t{0}, uint32_t{}), max32);
    
    // MAX - MAX = 0
    EXPECT_EQ(m::math::subtract(max32, max32, uint32_t{}), 0u);
    
    // MAX - 1 = MAX - 1
    EXPECT_EQ(m::math::subtract(max32, uint32_t{1}, uint32_t{}), max32 - 1);
}

TEST(SubtractionUnsignedUnsigned, DifferentSizedTypes)
{
    // uint32_t - uint8_t -> uint32_t
    EXPECT_EQ(m::math::subtract(uint32_t{200}, uint8_t{50}, uint32_t{}), 150u);
    
    // uint16_t - uint8_t -> uint32_t
    EXPECT_EQ(m::math::subtract(uint16_t{1234}, uint8_t{234}, uint32_t{}), 1000u);
}

TEST(SubtractionUnsignedUnsigned, NarrowingResults)
{
    // Result fits in uint8_t
    EXPECT_EQ(m::math::subtract(uint32_t{200}, uint32_t{50}, uint8_t{}), 150);
    
    // Result doesn't fit in uint8_t (too large)
    EXPECT_THROW(m::math::subtract(uint32_t{300}, uint32_t{0}, uint8_t{}), std::overflow_error);
    
    // Result would be negative (overflow)
    EXPECT_THROW(m::math::subtract(uint32_t{50}, uint32_t{100}, uint8_t{}), std::overflow_error);
}

// ============================================================================
// Unsigned - Unsigned -> Signed
// ============================================================================

TEST(SubtractionUnsignedUnsignedToSigned, BasicSubtraction)
{
    EXPECT_EQ(m::math::subtract(uint32_t{50}, uint32_t{20}, int32_t{}), 30);
    EXPECT_EQ(m::math::subtract(uint32_t{100}, uint32_t{200}, int32_t{}), -100);
}

TEST(SubtractionUnsignedUnsignedToSigned, NegativeResults)
{
    // Result is negative (allowed for signed result)
    EXPECT_EQ(m::math::subtract(uint32_t{10}, uint32_t{20}, int32_t{}), -10);
    EXPECT_EQ(m::math::subtract(uint32_t{0}, uint32_t{100}, int32_t{}), -100);
}

TEST(SubtractionUnsignedUnsignedToSigned, OverflowCases)
{
    // Very large unsigned values might overflow signed result
    constexpr auto max_unsigned = (std::numeric_limits<uint32_t>::max)();
    constexpr auto max_signed = (std::numeric_limits<int32_t>::max)();
    
    uint32_t large = static_cast<uint32_t>(max_signed) + 2;
    EXPECT_THROW(m::math::subtract(large, uint32_t{0}, int32_t{}), std::overflow_error);
}

// ============================================================================
// Unsigned - Signed -> Unsigned
// ============================================================================

TEST(SubtractionUnsignedSignedToUnsigned, PositiveSigned)
{
    EXPECT_EQ(m::math::subtract(uint32_t{100}, int32_t{50}, uint32_t{}), 50u);
    EXPECT_EQ(m::math::subtract(uint32_t{1000}, int32_t{500}, uint32_t{}), 500u);
    
    // l < r (result would be negative)
    EXPECT_THROW(m::math::subtract(uint32_t{50}, int32_t{100}, uint32_t{}), std::overflow_error);
}

TEST(SubtractionUnsignedSignedToUnsigned, NegativeSigned)
{
    // Subtracting negative is like adding positive
    EXPECT_EQ(m::math::subtract(uint32_t{100}, int32_t{-50}, uint32_t{}), 150u);
    EXPECT_EQ(m::math::subtract(uint32_t{1000}, int32_t{-500}, uint32_t{}), 1500u);
}

TEST(SubtractionUnsignedSignedToUnsigned, ZeroAndEdgeCases)
{
    EXPECT_EQ(m::math::subtract(uint32_t{42}, int32_t{0}, uint32_t{}), 42u);
    EXPECT_EQ(m::math::subtract(uint32_t{42}, int32_t{42}, uint32_t{}), 0u);
}

// ============================================================================
// Unsigned - Signed -> Signed
// ============================================================================

TEST(SubtractionUnsignedSignedToSigned, PositiveSigned)
{
    EXPECT_EQ(m::math::subtract(uint32_t{100}, int32_t{50}, int32_t{}), 50);
    EXPECT_EQ(m::math::subtract(uint32_t{50}, int32_t{100}, int32_t{}), -50);
}

TEST(SubtractionUnsignedSignedToSigned, NegativeSigned)
{
    EXPECT_EQ(m::math::subtract(uint32_t{100}, int32_t{-50}, int32_t{}), 150);
    EXPECT_EQ(m::math::subtract(uint32_t{50}, int32_t{-50}, int32_t{}), 100);
}

TEST(SubtractionUnsignedSignedToSigned, OverflowCases)
{
    constexpr auto max_signed = (std::numeric_limits<int32_t>::max)();
    uint32_t large = static_cast<uint32_t>(max_signed);
    
    // Large - (-1) would overflow
    EXPECT_THROW(m::math::subtract(large, int32_t{-2}, int32_t{}), std::overflow_error);
}

// ============================================================================
// Signed - Unsigned -> Unsigned
// ============================================================================

TEST(SubtractionSignedUnsignedToUnsigned, PositiveSigned)
{
    EXPECT_EQ(m::math::subtract(int32_t{100}, uint32_t{50}, uint32_t{}), 50u);
    EXPECT_EQ(m::math::subtract(int32_t{1000}, uint32_t{500}, uint32_t{}), 500u);
    
    // Result would be negative
    EXPECT_THROW(m::math::subtract(int32_t{50}, uint32_t{100}, uint32_t{}), std::overflow_error);
}

TEST(SubtractionSignedUnsignedToUnsigned, NegativeSigned)
{
    // Negative - unsigned always gives negative result (overflow for unsigned)
    EXPECT_THROW(m::math::subtract(int32_t{-10}, uint32_t{50}, uint32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::subtract(int32_t{-100}, uint32_t{0}, uint32_t{}), std::overflow_error);
}

TEST(SubtractionSignedUnsignedToUnsigned, ZeroResult)
{
    EXPECT_EQ(m::math::subtract(int32_t{50}, uint32_t{50}, uint32_t{}), 0u);
}

// ============================================================================
// Signed - Unsigned -> Signed
// ============================================================================

TEST(SubtractionSignedUnsignedToSigned, PositiveSigned)
{
    EXPECT_EQ(m::math::subtract(int32_t{100}, uint32_t{50}, int32_t{}), 50);
    EXPECT_EQ(m::math::subtract(int32_t{1000}, uint32_t{2000}, int32_t{}), -1000);
}

TEST(SubtractionSignedUnsignedToSigned, NegativeSigned)
{
    EXPECT_EQ(m::math::subtract(int32_t{-50}, uint32_t{50}, int32_t{}), -100);
    EXPECT_EQ(m::math::subtract(int32_t{-100}, uint32_t{50}, int32_t{}), -150);
}

TEST(SubtractionSignedUnsignedToSigned, OverflowCases)
{
    constexpr auto min_signed = (std::numeric_limits<int32_t>::min)();
    
    // INT_MIN - large unsigned should overflow
    EXPECT_THROW(m::math::subtract(min_signed, uint32_t{1}, int32_t{}), std::overflow_error);
}

// ============================================================================
// Signed - Signed -> Signed (already tested in signed_signed_to_signed.cpp)
// Adding a few more edge cases here
// ============================================================================

TEST(SubtractionSignedSigned, BasicSubtraction)
{
    EXPECT_EQ(m::math::subtract(int32_t{50}, int32_t{20}, int32_t{}), 30);
    EXPECT_EQ(m::math::subtract(int32_t{-10}, int32_t{20}, int32_t{}), -30);
    EXPECT_EQ(m::math::subtract(int32_t{-10}, int32_t{-20}, int32_t{}), 10);
}

TEST(SubtractionSignedSigned, OverflowPositive)
{
    constexpr auto max_signed = (std::numeric_limits<int32_t>::max)();
    constexpr auto min_signed = (std::numeric_limits<int32_t>::min)();
    
    // MAX - (-1) should overflow
    EXPECT_THROW(m::math::subtract(max_signed, int32_t{-1}, int32_t{}), std::overflow_error);
    
    // MAX - MIN should overflow (result would be > INT_MAX)
    EXPECT_THROW(m::math::subtract(max_signed, min_signed, int32_t{}), std::overflow_error);
}

TEST(SubtractionSignedSigned, OverflowNegative)
{
    constexpr auto min_signed = (std::numeric_limits<int32_t>::min)();
    
    // MIN - 1 should overflow
    EXPECT_THROW(m::math::subtract(min_signed, int32_t{1}, int32_t{}), std::overflow_error);
    
    // MIN - MAX should overflow
    constexpr auto max_signed = (std::numeric_limits<int32_t>::max)();
    EXPECT_THROW(m::math::subtract(min_signed, max_signed, int32_t{}), std::overflow_error);
}

// ============================================================================
// All Integer Sizes Tests
// ============================================================================

TEST(SubtractionAllSizes, Int8Subtraction)
{
    EXPECT_EQ(m::math::subtract(int8_t{100}, int8_t{50}, int8_t{}), 50);
    EXPECT_THROW(m::math::subtract(int8_t{-128}, int8_t{1}, int8_t{}), std::overflow_error);
    EXPECT_THROW(m::math::subtract(int8_t{127}, int8_t{-1}, int8_t{}), std::overflow_error);
}

TEST(SubtractionAllSizes, UInt8Subtraction)
{
    EXPECT_EQ(m::math::subtract(uint8_t{200}, uint8_t{100}, uint8_t{}), 100);
    EXPECT_THROW(m::math::subtract(uint8_t{100}, uint8_t{200}, uint8_t{}), std::overflow_error);
}

TEST(SubtractionAllSizes, Int16Subtraction)
{
    EXPECT_EQ(m::math::subtract(int16_t{3000}, int16_t{2000}, int16_t{}), 1000);
    EXPECT_THROW(m::math::subtract(int16_t{-32768}, int16_t{1}, int16_t{}), std::overflow_error);
}

TEST(SubtractionAllSizes, UInt16Subtraction)
{
    EXPECT_EQ(m::math::subtract(uint16_t{60000}, uint16_t{30000}, uint16_t{}), 30000);
    EXPECT_THROW(m::math::subtract(uint16_t{30000}, uint16_t{60000}, uint16_t{}), std::overflow_error);
}

TEST(SubtractionAllSizes, Int64Subtraction)
{
    EXPECT_EQ(m::math::subtract(int64_t{3000000000}, int64_t{2000000000}, int64_t{}), 1000000000LL);
    
    constexpr auto min64 = (std::numeric_limits<int64_t>::min)();
    EXPECT_THROW(m::math::subtract(min64, int64_t{1}, int64_t{}), std::overflow_error);
}

TEST(SubtractionAllSizes, UInt64Subtraction)
{
    EXPECT_EQ(m::math::subtract(uint64_t{3000000000}, uint64_t{2000000000}, uint64_t{}), 1000000000ULL);
    EXPECT_THROW(m::math::subtract(uint64_t{1000}, uint64_t{2000}, uint64_t{}), std::overflow_error);
}

// ============================================================================
// Special Edge Cases
// ============================================================================

TEST(SubtractionEdgeCases, SubtractSelf)
{
    EXPECT_EQ(m::math::subtract(uint32_t{42}, uint32_t{42}, uint32_t{}), 0u);
    EXPECT_EQ(m::math::subtract(int32_t{-42}, int32_t{-42}, int32_t{}), 0);
    EXPECT_EQ(m::math::subtract(int32_t{42}, int32_t{42}, int32_t{}), 0);
}

TEST(SubtractionEdgeCases, SubtractZero)
{
    EXPECT_EQ(m::math::subtract(uint32_t{42}, uint32_t{0}, uint32_t{}), 42u);
    EXPECT_EQ(m::math::subtract(int32_t{42}, int32_t{0}, int32_t{}), 42);
    EXPECT_EQ(m::math::subtract(int32_t{-42}, int32_t{0}, int32_t{}), -42);
}
