// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>

#include <m/math/math.h>

//
// Comprehensive tests for division operations
// These tests verify the fixes for issue #4 in CHECKLIST.md
//

// ============================================================================
// Unsigned / Unsigned -> Unsigned
// ============================================================================

TEST(DivisionUnsignedUnsigned, BasicDivision)
{
    EXPECT_EQ(m::math::divide(uint32_t{10}, uint32_t{2}, uint32_t{}), 5u);
    EXPECT_EQ(m::math::divide(uint32_t{100}, uint32_t{10}, uint32_t{}), 10u);
    EXPECT_EQ(m::math::divide(uint32_t{7}, uint32_t{3}, uint32_t{}), 2u); // Integer division
}

TEST(DivisionUnsignedUnsigned, DivisionByZero)
{
    EXPECT_THROW(m::math::divide(uint32_t{10}, uint32_t{0}, uint32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::divide(uint32_t{0}, uint32_t{0}, uint32_t{}), std::overflow_error);
}

TEST(DivisionUnsignedUnsigned, DivisionByOne)
{
    EXPECT_EQ(m::math::divide(uint32_t{42}, uint32_t{1}, uint32_t{}), 42u);
}

TEST(DivisionUnsignedUnsigned, DivisionByItself)
{
    EXPECT_EQ(m::math::divide(uint32_t{42}, uint32_t{42}, uint32_t{}), 1u);
}

TEST(DivisionUnsignedUnsigned, ZeroDividedByNonZero)
{
    EXPECT_EQ(m::math::divide(uint32_t{0}, uint32_t{10}, uint32_t{}), 0u);
}

TEST(DivisionUnsignedUnsigned, NarrowingResult)
{
    // 200 / 2 = 100 fits in uint8_t
    EXPECT_EQ(m::math::divide(uint32_t{200}, uint32_t{2}, uint8_t{}), 100);
    
    // 1000 / 2 = 500 doesn't fit in uint8_t
    EXPECT_THROW(m::math::divide(uint32_t{1000}, uint32_t{2}, uint8_t{}), std::overflow_error);
}

// ============================================================================
// Signed / Signed -> Signed
// ============================================================================

TEST(DivisionSignedSigned, BasicDivision)
{
    EXPECT_EQ(m::math::divide(int32_t{10}, int32_t{2}, int32_t{}), 5);
    EXPECT_EQ(m::math::divide(int32_t{-10}, int32_t{2}, int32_t{}), -5);
    EXPECT_EQ(m::math::divide(int32_t{10}, int32_t{-2}, int32_t{}), -5);
    EXPECT_EQ(m::math::divide(int32_t{-10}, int32_t{-2}, int32_t{}), 5);
}

TEST(DivisionSignedSigned, DivisionByZero)
{
    EXPECT_THROW(m::math::divide(int32_t{10}, int32_t{0}, int32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::divide(int32_t{-10}, int32_t{0}, int32_t{}), std::overflow_error);
}

TEST(DivisionSignedSigned, IntMinDividedByMinusOne)
{
    // This is the classic overflow case: INT_MIN / -1 = INT_MAX + 1
    constexpr auto min32 = (std::numeric_limits<int32_t>::min)();
    EXPECT_THROW(m::math::divide(min32, int32_t{-1}, int32_t{}), std::overflow_error);
}

TEST(DivisionSignedSigned, IntMinDividedByOther)
{
    constexpr auto min32 = (std::numeric_limits<int32_t>::min)();
    
    // INT_MIN / 1 = INT_MIN (should work)
    EXPECT_EQ(m::math::divide(min32, int32_t{1}, int32_t{}), min32);
    
    // INT_MIN / 2 (should work)
    EXPECT_EQ(m::math::divide(min32, int32_t{2}, int32_t{}), min32 / 2);
    
    // INT_MIN / -2 (should work)
    EXPECT_EQ(m::math::divide(min32, int32_t{-2}, int32_t{}), min32 / -2);
}

TEST(DivisionSignedSigned, DivisionByItself)
{
    EXPECT_EQ(m::math::divide(int32_t{42}, int32_t{42}, int32_t{}), 1);
    EXPECT_EQ(m::math::divide(int32_t{-42}, int32_t{-42}, int32_t{}), 1);
}

// ============================================================================
// Unsigned / Signed -> Unsigned
// ============================================================================

TEST(DivisionUnsignedSignedToUnsigned, PositiveDivisor)
{
    EXPECT_EQ(m::math::divide(uint32_t{10}, int32_t{2}, uint32_t{}), 5u);
    EXPECT_EQ(m::math::divide(uint32_t{100}, int32_t{10}, uint32_t{}), 10u);
}

TEST(DivisionUnsignedSignedToUnsigned, NegativeDivisor)
{
    // Unsigned / negative = negative result (can't fit in unsigned)
    EXPECT_THROW(m::math::divide(uint32_t{10}, int32_t{-2}, uint32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::divide(uint32_t{0}, int32_t{-1}, uint32_t{}), std::overflow_error);
}

TEST(DivisionUnsignedSignedToUnsigned, DivisionByZero)
{
    EXPECT_THROW(m::math::divide(uint32_t{10}, int32_t{0}, uint32_t{}), std::overflow_error);
}

// ============================================================================
// Signed / Unsigned -> Unsigned
// ============================================================================

TEST(DivisionSignedUnsignedToUnsigned, PositiveDividend)
{
    EXPECT_EQ(m::math::divide(int32_t{10}, uint32_t{2}, uint32_t{}), 5u);
    EXPECT_EQ(m::math::divide(int32_t{100}, uint32_t{10}, uint32_t{}), 10u);
}

TEST(DivisionSignedUnsignedToUnsigned, NegativeDividend)
{
    // Negative / unsigned = negative result (can't fit in unsigned)
    EXPECT_THROW(m::math::divide(int32_t{-10}, uint32_t{2}, uint32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::divide(int32_t{-1}, uint32_t{10}, uint32_t{}), std::overflow_error);
}

TEST(DivisionSignedUnsignedToUnsigned, ZeroDividend)
{
    EXPECT_EQ(m::math::divide(int32_t{0}, uint32_t{10}, uint32_t{}), 0u);
}

TEST(DivisionSignedUnsignedToUnsigned, DivisionByZero)
{
    EXPECT_THROW(m::math::divide(int32_t{10}, uint32_t{0}, uint32_t{}), std::overflow_error);
}

// ============================================================================
// Signed / Unsigned -> Signed
// ============================================================================

TEST(DivisionSignedUnsignedToSigned, PositiveDividend)
{
    EXPECT_EQ(m::math::divide(int32_t{10}, uint32_t{2}, int32_t{}), 5);
    EXPECT_EQ(m::math::divide(int32_t{100}, uint32_t{10}, int32_t{}), 10);
}

TEST(DivisionSignedUnsignedToSigned, NegativeDividend)
{
    EXPECT_EQ(m::math::divide(int32_t{-10}, uint32_t{2}, int32_t{}), -5);
    EXPECT_EQ(m::math::divide(int32_t{-100}, uint32_t{10}, int32_t{}), -10);
}

TEST(DivisionSignedUnsignedToSigned, IntMinDividend)
{
    constexpr auto min32 = (std::numeric_limits<int32_t>::min)();
    
    // INT_MIN / 1 should work
    EXPECT_EQ(m::math::divide(min32, uint32_t{1}, int32_t{}), min32);
    
    // INT_MIN / 2 should work
    EXPECT_EQ(m::math::divide(min32, uint32_t{2}, int32_t{}), min32 / 2);
}

TEST(DivisionSignedUnsignedToSigned, DivisionByZero)
{
    EXPECT_THROW(m::math::divide(int32_t{10}, uint32_t{0}, int32_t{}), std::overflow_error);
}

// ============================================================================
// Unsigned / Signed -> Signed
// ============================================================================

TEST(DivisionUnsignedSignedToSigned, PositiveDivisor)
{
    EXPECT_EQ(m::math::divide(uint32_t{10}, int32_t{2}, int32_t{}), 5);
    EXPECT_EQ(m::math::divide(uint32_t{100}, int32_t{10}, int32_t{}), 10);
}

TEST(DivisionUnsignedSignedToSigned, NegativeDivisor)
{
    EXPECT_EQ(m::math::divide(uint32_t{10}, int32_t{-2}, int32_t{}), -5);
    EXPECT_EQ(m::math::divide(uint32_t{100}, int32_t{-10}, int32_t{}), -10);
}

TEST(DivisionUnsignedSignedToSigned, IntMinDivisor)
{
    constexpr auto min32 = (std::numeric_limits<int32_t>::min)();
    
    // Small dividend / INT_MIN should give 0 (integer division)
    EXPECT_EQ(m::math::divide(uint32_t{100}, min32, int32_t{}), 0);
    
    // Large dividend / INT_MIN should give result.
    // IMPORTANT: `large` MUST be uint64_t, not uint32_t.
    // 2 * |INT32_MIN| = 2 * 2147483648 = 4294967296 = 2^32, which exceeds UINT32_MAX.
    // If declared as uint32_t, the multiplication wraps to 0 in C++ unsigned arithmetic
    // *before* the call, so divide(0, INT32_MIN, int32_t{}) would correctly return 0,
    // giving a false pass on the wrong assertion value instead of testing the intended case.
    uint64_t large = static_cast<uint64_t>(-(static_cast<int64_t>(min32))) * 2;
    EXPECT_EQ(m::math::divide(large, min32, int32_t{}), -2);
}

TEST(DivisionUnsignedSignedToSigned, DivisionByZero)
{
    EXPECT_THROW(m::math::divide(uint32_t{10}, int32_t{0}, int32_t{}), std::overflow_error);
}

// ============================================================================
// Unsigned / Unsigned -> Signed
// ============================================================================

TEST(DivisionUnsignedUnsignedToSigned, BasicDivision)
{
    EXPECT_EQ(m::math::divide(uint32_t{10}, uint32_t{2}, int32_t{}), 5);
    EXPECT_EQ(m::math::divide(uint32_t{100}, uint32_t{10}, int32_t{}), 10);
}

TEST(DivisionUnsignedUnsignedToSigned, LargeValues)
{
    // Values that fit in signed
    EXPECT_EQ(m::math::divide(uint32_t{1000}, uint32_t{10}, int32_t{}), 100);
}

TEST(DivisionUnsignedUnsignedToSigned, DivisionByZero)
{
    EXPECT_THROW(m::math::divide(uint32_t{10}, uint32_t{0}, int32_t{}), std::overflow_error);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(DivisionEdgeCases, DifferentSizedTypes)
{
    // int8_t / int8_t -> int32_t
    EXPECT_EQ(m::math::divide(int8_t{100}, int8_t{10}, int32_t{}), 10);
    EXPECT_EQ(m::math::divide(int8_t{-100}, int8_t{10}, int32_t{}), -10);
    
    // uint8_t / uint8_t -> uint32_t
    EXPECT_EQ(m::math::divide(uint8_t{200}, uint8_t{10}, uint32_t{}), 20u);
}

TEST(DivisionEdgeCases, MaxValues)
{
    constexpr auto max32 = (std::numeric_limits<int32_t>::max)();
    constexpr auto max32u = (std::numeric_limits<uint32_t>::max)();
    
    // INT_MAX / 1 = INT_MAX
    EXPECT_EQ(m::math::divide(max32, int32_t{1}, int32_t{}), max32);
    
    // UINT_MAX / 1 = UINT_MAX
    EXPECT_EQ(m::math::divide(max32u, uint32_t{1}, uint32_t{}), max32u);
    
    // INT_MAX / INT_MAX = 1
    EXPECT_EQ(m::math::divide(max32, max32, int32_t{}), 1);
}
