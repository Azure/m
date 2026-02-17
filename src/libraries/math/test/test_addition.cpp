// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>

#include <m/math/math.h>

//
// Comprehensive tests for addition operations across all type combinations
// Tests cover: unsigned+unsigned, unsigned+signed, signed+unsigned, signed+signed
// For all result types: unsigned and signed
//

// ============================================================================
// Unsigned + Unsigned -> Unsigned
// ============================================================================

TEST(AdditionUnsignedUnsigned, BasicAddition)
{
    EXPECT_EQ(m::math::add(uint32_t{10}, uint32_t{20}, uint32_t{}), 30u);
    EXPECT_EQ(m::math::add(uint32_t{0}, uint32_t{0}, uint32_t{}), 0u);
    EXPECT_EQ(m::math::add(uint32_t{1}, uint32_t{1}, uint32_t{}), 2u);
}

TEST(AdditionUnsignedUnsigned, AdditionWithZero)
{
    EXPECT_EQ(m::math::add(uint32_t{42}, uint32_t{0}, uint32_t{}), 42u);
    EXPECT_EQ(m::math::add(uint32_t{0}, uint32_t{42}, uint32_t{}), 42u);
}

TEST(AdditionUnsignedUnsigned, MaxValueEdgeCases)
{
    constexpr auto max32 = (std::numeric_limits<uint32_t>::max)();
    
    // MAX + 0 = MAX (should work)
    EXPECT_EQ(m::math::add(max32, uint32_t{0}, uint32_t{}), max32);
    
    // MAX + 1 should overflow
    EXPECT_THROW(m::math::add(max32, uint32_t{1}, uint32_t{}), std::overflow_error);
    
    // MAX + MAX should overflow
    EXPECT_THROW(m::math::add(max32, max32, uint32_t{}), std::overflow_error);
}

TEST(AdditionUnsignedUnsigned, DifferentSizedTypes)
{
    // uint8_t + uint8_t -> uint32_t
    EXPECT_EQ(m::math::add(uint8_t{100}, uint8_t{50}, uint32_t{}), 150u);
    
    // uint16_t + uint8_t -> uint32_t
    EXPECT_EQ(m::math::add(uint16_t{1000}, uint8_t{234}, uint32_t{}), 1234u);
    
    // uint32_t + uint32_t -> uint64_t
    EXPECT_EQ(m::math::add(uint32_t{1000000}, uint32_t{2000000}, uint64_t{}), 3000000ull);
}

TEST(AdditionUnsignedUnsigned, NarrowingResults)
{
    // Result fits in uint8_t
    EXPECT_EQ(m::math::add(uint32_t{100}, uint32_t{50}, uint8_t{}), 150);
    
    // Result doesn't fit in uint8_t
    EXPECT_THROW(m::math::add(uint32_t{200}, uint32_t{100}, uint8_t{}), std::overflow_error);
    
    // uint16_t overflow to uint8_t
    EXPECT_THROW(m::math::add(uint16_t{256}, uint16_t{0}, uint8_t{}), std::overflow_error);
}

// ============================================================================
// Unsigned + Unsigned -> Signed
// ============================================================================

TEST(AdditionUnsignedUnsignedToSigned, BasicAddition)
{
    EXPECT_EQ(m::math::add(uint32_t{10}, uint32_t{20}, int32_t{}), 30);
    EXPECT_EQ(m::math::add(uint32_t{100}, uint32_t{200}, int32_t{}), 300);
}

TEST(AdditionUnsignedUnsignedToSigned, LargeValues)
{
    constexpr auto max_signed = (std::numeric_limits<int32_t>::max)();
    
    // Values that fit in signed
    EXPECT_EQ(m::math::add(uint32_t{1000000}, uint32_t{1000000}, int32_t{}), 2000000);
    
    // Values that exceed INT_MAX
    uint32_t large = static_cast<uint32_t>(max_signed);
    EXPECT_THROW(m::math::add(large, uint32_t{2}, int32_t{}), std::overflow_error);
}

// ============================================================================
// Unsigned + Signed -> Unsigned
// ============================================================================

TEST(AdditionUnsignedSignedToUnsigned, PositiveSigned)
{
    EXPECT_EQ(m::math::add(uint32_t{100}, int32_t{50}, uint32_t{}), 150u);
    EXPECT_EQ(m::math::add(uint32_t{1000}, int32_t{2000}, uint32_t{}), 3000u);
}

TEST(AdditionUnsignedSignedToUnsigned, NegativeSigned)
{
    // Positive result (unsigned - |negative|)
    EXPECT_EQ(m::math::add(uint32_t{100}, int32_t{-50}, uint32_t{}), 50u);
    EXPECT_EQ(m::math::add(uint32_t{1000}, int32_t{-500}, uint32_t{}), 500u);
    
    // Result would be negative (overflow)
    EXPECT_THROW(m::math::add(uint32_t{50}, int32_t{-100}, uint32_t{}), std::overflow_error);
}

TEST(AdditionUnsignedSignedToUnsigned, ZeroAndEdgeCases)
{
    EXPECT_EQ(m::math::add(uint32_t{42}, int32_t{0}, uint32_t{}), 42u);
    EXPECT_EQ(m::math::add(uint32_t{0}, int32_t{42}, uint32_t{}), 42u);
    
    // INT_MIN edge case
    constexpr auto min_signed = (std::numeric_limits<int32_t>::min)();
    uint32_t abs_min = static_cast<uint32_t>(-(static_cast<int64_t>(min_signed)));
    EXPECT_EQ(m::math::add(abs_min, min_signed, uint32_t{}), 0u);
}

// ============================================================================
// Unsigned + Signed -> Signed
// ============================================================================

TEST(AdditionUnsignedSignedToSigned, PositiveSigned)
{
    EXPECT_EQ(m::math::add(uint32_t{100}, int32_t{50}, int32_t{}), 150);
    EXPECT_EQ(m::math::add(uint32_t{1000}, int32_t{2000}, int32_t{}), 3000);
}

TEST(AdditionUnsignedSignedToSigned, NegativeSigned)
{
    EXPECT_EQ(m::math::add(uint32_t{100}, int32_t{-50}, int32_t{}), 50);
    EXPECT_EQ(m::math::add(uint32_t{1000}, int32_t{-500}, int32_t{}), 500);
    
    // Large negative
    EXPECT_EQ(m::math::add(uint32_t{50}, int32_t{-100}, int32_t{}), -50);
}

TEST(AdditionUnsignedSignedToSigned, OverflowCases)
{
    constexpr auto max_signed = (std::numeric_limits<int32_t>::max)();
    uint32_t large = static_cast<uint32_t>(max_signed);
    
    EXPECT_THROW(m::math::add(large, int32_t{2}, int32_t{}), std::overflow_error);
}

// ============================================================================
// Signed + Unsigned -> Unsigned
// ============================================================================

TEST(AdditionSignedUnsignedToUnsigned, PositiveSigned)
{
    EXPECT_EQ(m::math::add(int32_t{100}, uint32_t{50}, uint32_t{}), 150u);
    EXPECT_EQ(m::math::add(int32_t{1000}, uint32_t{2000}, uint32_t{}), 3000u);
}

TEST(AdditionSignedUnsignedToUnsigned, NegativeSigned)
{
    // Result is positive
    EXPECT_EQ(m::math::add(int32_t{-50}, uint32_t{100}, uint32_t{}), 50u);
    
    // Result would be negative (overflow)
    EXPECT_THROW(m::math::add(int32_t{-100}, uint32_t{50}, uint32_t{}), std::overflow_error);
}

TEST(AdditionSignedUnsignedToUnsigned, ZeroResult)
{
    EXPECT_EQ(m::math::add(int32_t{-50}, uint32_t{50}, uint32_t{}), 0u);
}

// ============================================================================
// Signed + Unsigned -> Signed
// ============================================================================

TEST(AdditionSignedUnsignedToSigned, PositiveSigned)
{
    EXPECT_EQ(m::math::add(int32_t{100}, uint32_t{50}, int32_t{}), 150);
    EXPECT_EQ(m::math::add(int32_t{1000}, uint32_t{2000}, int32_t{}), 3000);
}

TEST(AdditionSignedUnsignedToSigned, NegativeSigned)
{
    EXPECT_EQ(m::math::add(int32_t{-50}, uint32_t{100}, int32_t{}), 50);
    EXPECT_EQ(m::math::add(int32_t{-100}, uint32_t{50}, int32_t{}), -50);
}

TEST(AdditionSignedUnsignedToSigned, OverflowCases)
{
    constexpr auto max_signed = (std::numeric_limits<int32_t>::max)();
    
    EXPECT_THROW(m::math::add(max_signed, uint32_t{1}, int32_t{}), std::overflow_error);
}

// ============================================================================
// Signed + Signed -> Signed (already tested in signed_signed_to_signed.cpp)
// Adding a few more edge cases here
// ============================================================================

TEST(AdditionSignedSigned, BasicAddition)
{
    EXPECT_EQ(m::math::add(int32_t{10}, int32_t{20}, int32_t{}), 30);
    EXPECT_EQ(m::math::add(int32_t{-10}, int32_t{20}, int32_t{}), 10);
    EXPECT_EQ(m::math::add(int32_t{-10}, int32_t{-20}, int32_t{}), -30);
}

TEST(AdditionSignedSigned, OverflowPositive)
{
    constexpr auto max_signed = (std::numeric_limits<int32_t>::max)();
    
    EXPECT_THROW(m::math::add(max_signed, int32_t{1}, int32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::add(max_signed, max_signed, int32_t{}), std::overflow_error);
}

TEST(AdditionSignedSigned, OverflowNegative)
{
    constexpr auto min_signed = (std::numeric_limits<int32_t>::min)();
    
    EXPECT_THROW(m::math::add(min_signed, int32_t{-1}, int32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::add(min_signed, min_signed, int32_t{}), std::overflow_error);
}

// ============================================================================
// All Integer Sizes Tests
// ============================================================================

TEST(AdditionAllSizes, Int8Addition)
{
    EXPECT_EQ(m::math::add(int8_t{50}, int8_t{50}, int8_t{}), 100);
    EXPECT_THROW(m::math::add(int8_t{127}, int8_t{1}, int8_t{}), std::overflow_error);
    EXPECT_THROW(m::math::add(int8_t{-128}, int8_t{-1}, int8_t{}), std::overflow_error);
}

TEST(AdditionAllSizes, UInt8Addition)
{
    EXPECT_EQ(m::math::add(uint8_t{100}, uint8_t{100}, uint8_t{}), 200);
    EXPECT_THROW(m::math::add(uint8_t{255}, uint8_t{1}, uint8_t{}), std::overflow_error);
}

TEST(AdditionAllSizes, Int16Addition)
{
    EXPECT_EQ(m::math::add(int16_t{1000}, int16_t{2000}, int16_t{}), 3000);
    EXPECT_THROW(m::math::add(int16_t{32767}, int16_t{1}, int16_t{}), std::overflow_error);
}

TEST(AdditionAllSizes, UInt16Addition)
{
    EXPECT_EQ(m::math::add(uint16_t{30000}, uint16_t{30000}, uint16_t{}), 60000);
    EXPECT_THROW(m::math::add(uint16_t{65535}, uint16_t{1}, uint16_t{}), std::overflow_error);
}

TEST(AdditionAllSizes, Int64Addition)
{
    EXPECT_EQ(m::math::add(int64_t{1000000000}, int64_t{2000000000}, int64_t{}), 3000000000LL);
    
    constexpr auto max64 = (std::numeric_limits<int64_t>::max)();
    EXPECT_THROW(m::math::add(max64, int64_t{1}, int64_t{}), std::overflow_error);
}

TEST(AdditionAllSizes, UInt64Addition)
{
    EXPECT_EQ(m::math::add(uint64_t{1000000000}, uint64_t{2000000000}, uint64_t{}), 3000000000ULL);
    
    constexpr auto max64 = (std::numeric_limits<uint64_t>::max)();
    EXPECT_THROW(m::math::add(max64, uint64_t{1}, uint64_t{}), std::overflow_error);
}
