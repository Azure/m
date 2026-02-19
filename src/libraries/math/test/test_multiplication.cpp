// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>

#include <m/math/math.h>

//
// Comprehensive tests for multiplication operations
// These tests verify the fixes for issue #5 in CHECKLIST.md
// NOTE: Some specializations are not yet implemented
//

// ============================================================================
// Unsigned × Unsigned -> Unsigned (IMPLEMENTED)
// ============================================================================

TEST(MultiplicationUnsignedUnsigned, BasicMultiplication)
{
    EXPECT_EQ(m::math::multiply(uint32_t{10}, uint32_t{2}, uint32_t{}), 20u);
    EXPECT_EQ(m::math::multiply(uint32_t{100}, uint32_t{10}, uint32_t{}), 1000u);
}

TEST(MultiplicationUnsignedUnsigned, MultiplicationByZero)
{
    EXPECT_EQ(m::math::multiply(uint32_t{10}, uint32_t{0}, uint32_t{}), 0u);
    EXPECT_EQ(m::math::multiply(uint32_t{0}, uint32_t{10}, uint32_t{}), 0u);
    EXPECT_EQ(m::math::multiply(uint32_t{0}, uint32_t{0}, uint32_t{}), 0u);
}

TEST(MultiplicationUnsignedUnsigned, MultiplicationByOne)
{
    EXPECT_EQ(m::math::multiply(uint32_t{42}, uint32_t{1}, uint32_t{}), 42u);
    EXPECT_EQ(m::math::multiply(uint32_t{1}, uint32_t{42}, uint32_t{}), 42u);
}

TEST(MultiplicationUnsignedUnsigned, Overflow)
{
    constexpr auto max32 = (std::numeric_limits<uint32_t>::max)();
    
    // max × 2 should overflow
    EXPECT_THROW(m::math::multiply(max32, uint32_t{2}, uint32_t{}), std::overflow_error);
    
    // Large × large should overflow
    EXPECT_THROW(m::math::multiply(max32 / 2, uint32_t{3}, uint32_t{}), std::overflow_error);
}

// ============================================================================
// Unsigned × Unsigned -> Signed (IMPLEMENTED)
// ============================================================================

TEST(MultiplicationUnsignedUnsignedToSigned, BasicMultiplication)
{
    EXPECT_EQ(m::math::multiply(uint32_t{10}, uint32_t{2}, int32_t{}), 20);
    EXPECT_EQ(m::math::multiply(uint32_t{100}, uint32_t{10}, int32_t{}), 1000);
}

TEST(MultiplicationUnsignedUnsignedToSigned, LargeValues)
{
    // Values that fit in signed
    EXPECT_EQ(m::math::multiply(uint32_t{1000}, uint32_t{1000}, int32_t{}), 1000000);
    
    // Values that exceed INT_MAX
    constexpr auto max32_signed = (std::numeric_limits<int32_t>::max)();
    uint32_t large = static_cast<uint32_t>(max32_signed) + 1;
    EXPECT_THROW(m::math::multiply(large, uint32_t{2}, int32_t{}), std::overflow_error);
}

// ============================================================================
// Unsigned × Signed -> Unsigned (IMPLEMENTED)
// ============================================================================

TEST(MultiplicationUnsignedSignedToUnsigned, PositiveMultiplier)
{
    EXPECT_EQ(m::math::multiply(uint32_t{10}, int32_t{2}, uint32_t{}), 20u);
    EXPECT_EQ(m::math::multiply(uint32_t{100}, int32_t{10}, uint32_t{}), 1000u);
}

TEST(MultiplicationUnsignedSignedToUnsigned, NegativeMultiplier)
{
    // Unsigned × negative = negative result (can't fit in unsigned)
    EXPECT_THROW(m::math::multiply(uint32_t{10}, int32_t{-2}, uint32_t{}), std::overflow_error);
    // 0 × (−1) = 0 in ℤ; 0 is representable in uint32_t — no overflow
    EXPECT_EQ(m::math::multiply(uint32_t{0}, int32_t{-1}, uint32_t{}), 0u);
}

TEST(MultiplicationUnsignedSignedToUnsigned, MultiplicationByZero)
{
    EXPECT_EQ(m::math::multiply(uint32_t{10}, int32_t{0}, uint32_t{}), 0u);
    EXPECT_EQ(m::math::multiply(uint32_t{0}, int32_t{10}, uint32_t{}), 0u);
}

// ============================================================================
// Unsigned × Signed -> Signed (NOW IMPLEMENTED)
// ============================================================================

TEST(MultiplicationUnsignedSignedToSigned, PositiveMultiplier)
{
    EXPECT_EQ(m::math::multiply(uint32_t{10}, int32_t{2}, int32_t{}), 20);
    EXPECT_EQ(m::math::multiply(uint32_t{100}, int32_t{10}, int32_t{}), 1000);
}

TEST(MultiplicationUnsignedSignedToSigned, NegativeMultiplier)
{
    EXPECT_EQ(m::math::multiply(uint32_t{10}, int32_t{-2}, int32_t{}), -20);
    EXPECT_EQ(m::math::multiply(uint32_t{100}, int32_t{-10}, int32_t{}), -1000);
}

TEST(MultiplicationUnsignedSignedToSigned, MultiplicationByZero)
{
    EXPECT_EQ(m::math::multiply(uint32_t{10}, int32_t{0}, int32_t{}), 0);
    EXPECT_EQ(m::math::multiply(uint32_t{0}, int32_t{-10}, int32_t{}), 0);
}

// ============================================================================
// Signed × Unsigned -> Unsigned (NOW IMPLEMENTED)
// ============================================================================

TEST(MultiplicationSignedUnsignedToUnsigned, PositiveMultiplicand)
{
    EXPECT_EQ(m::math::multiply(int32_t{10}, uint32_t{2}, uint32_t{}), 20u);
    EXPECT_EQ(m::math::multiply(int32_t{100}, uint32_t{10}, uint32_t{}), 1000u);
}

TEST(MultiplicationSignedUnsignedToUnsigned, NegativeMultiplicand)
{
    // Negative × unsigned = negative result (can't fit in unsigned)
    EXPECT_THROW(m::math::multiply(int32_t{-10}, uint32_t{2}, uint32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::multiply(int32_t{-1}, uint32_t{100}, uint32_t{}), std::overflow_error);
}

TEST(MultiplicationSignedUnsignedToUnsigned, MultiplicationByZero)
{
    EXPECT_EQ(m::math::multiply(int32_t{10}, uint32_t{0}, uint32_t{}), 0u);
    EXPECT_EQ(m::math::multiply(int32_t{0}, uint32_t{10}, uint32_t{}), 0u);
}

// ============================================================================
// Signed × Unsigned -> Signed (NOW IMPLEMENTED)
// ============================================================================

TEST(MultiplicationSignedUnsignedToSigned, PositiveMultiplicand)
{
    EXPECT_EQ(m::math::multiply(int32_t{10}, uint32_t{2}, int32_t{}), 20);
    EXPECT_EQ(m::math::multiply(int32_t{100}, uint32_t{10}, int32_t{}), 1000);
}

TEST(MultiplicationSignedUnsignedToSigned, NegativeMultiplicand)
{
    EXPECT_EQ(m::math::multiply(int32_t{-10}, uint32_t{2}, int32_t{}), -20);
    EXPECT_EQ(m::math::multiply(int32_t{-100}, uint32_t{10}, int32_t{}), -1000);
}

TEST(MultiplicationSignedUnsignedToSigned, MultiplicationByZero)
{
    EXPECT_EQ(m::math::multiply(int32_t{10}, uint32_t{0}, int32_t{}), 0);
    EXPECT_EQ(m::math::multiply(int32_t{0}, uint32_t{10}, int32_t{}), 0);
}

// ============================================================================
// Signed × Signed -> Signed (NOW IMPLEMENTED)
// ============================================================================

TEST(MultiplicationSignedSigned, BasicMultiplication)
{
    EXPECT_EQ(m::math::multiply(int32_t{10}, int32_t{2}, int32_t{}), 20);
    EXPECT_EQ(m::math::multiply(int32_t{-10}, int32_t{2}, int32_t{}), -20);
    EXPECT_EQ(m::math::multiply(int32_t{10}, int32_t{-2}, int32_t{}), -20);
    EXPECT_EQ(m::math::multiply(int32_t{-10}, int32_t{-2}, int32_t{}), 20);
}

TEST(MultiplicationSignedSigned, MultiplicationByZero)
{
    EXPECT_EQ(m::math::multiply(int32_t{10}, int32_t{0}, int32_t{}), 0);
    EXPECT_EQ(m::math::multiply(int32_t{0}, int32_t{-10}, int32_t{}), 0);
}

TEST(MultiplicationSignedSigned, IntMinOverflow)
{
    constexpr auto min32 = (std::numeric_limits<int32_t>::min)();

    // INT_MIN × -1 should overflow (result would be INT_MAX + 1)
    EXPECT_THROW(m::math::multiply(min32, int32_t{-1}, int32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::multiply(int32_t{-1}, min32, int32_t{}), std::overflow_error);

    // INT_MIN × 2 should overflow  
    EXPECT_THROW(m::math::multiply(min32, int32_t{2}, int32_t{}), std::overflow_error);

    // INT_MIN × -2 should overflow
    EXPECT_THROW(m::math::multiply(min32, int32_t{-2}, int32_t{}), std::overflow_error);
}

TEST(MultiplicationSignedSigned, LargeValues)
{
    constexpr auto max32 = (std::numeric_limits<int32_t>::max)();

    // Large × large should overflow
    EXPECT_THROW(m::math::multiply(max32, int32_t{2}, int32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::multiply(max32 / 2, int32_t{3}, int32_t{}), std::overflow_error);
}
