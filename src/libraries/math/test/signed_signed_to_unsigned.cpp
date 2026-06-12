// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>

#include <m/math/math.h>

//
// Tests for the (signed [op] signed) -> unsigned specialization.
//
// Per the library model: the operation is performed in ℤ and the result is
// then required to be representable in the unsigned ResultT (i.e. non-negative
// and within range), otherwise an overflow_error is thrown.
//

// ============================================================================
// add
// ============================================================================

TEST(SignedSignedToUnsigned, AddBothNonNegative)
{
    EXPECT_EQ(m::math::add(int32_t{20}, int32_t{22}, uint32_t{}), 42u);
    EXPECT_EQ(m::math::add(int32_t{0}, int32_t{0}, uint32_t{}), 0u);
}

TEST(SignedSignedToUnsigned, AddMixedSignsNonNegativeResult)
{
    EXPECT_EQ(m::math::add(int32_t{-5}, int32_t{12}, uint32_t{}), 7u);
    EXPECT_EQ(m::math::add(int32_t{12}, int32_t{-12}, uint32_t{}), 0u);
}

TEST(SignedSignedToUnsigned, AddNegativeResultThrows)
{
    EXPECT_THROW(m::math::add(int32_t{-5}, int32_t{2}, uint32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::add(int32_t{-1}, int32_t{-1}, uint32_t{}), std::overflow_error);
}

TEST(SignedSignedToUnsigned, AddWidenAvoidsFalseOverflow)
{
    constexpr auto max32 = (std::numeric_limits<int32_t>::max)();
    // max32 + max32 fits in uint32_t (and certainly in uint64_t).
    EXPECT_EQ(m::math::add(max32, max32, uint64_t{}),
              static_cast<uint64_t>(max32) + static_cast<uint64_t>(max32));
    EXPECT_EQ(m::math::add(max32, max32, uint32_t{}),
              static_cast<uint32_t>(max32) + static_cast<uint32_t>(max32));
}

TEST(SignedSignedToUnsigned, AddNarrowResultOverflow)
{
    // 200 + 100 = 300 does not fit in uint8_t.
    EXPECT_THROW(m::math::add(int32_t{200}, int32_t{100}, uint8_t{}), std::overflow_error);
    // 200 + 55 = 255 fits exactly.
    EXPECT_EQ(m::math::add(int32_t{200}, int32_t{55}, uint8_t{}), uint8_t{255});
}

TEST(SignedSignedToUnsigned, AddMostNegativeOperand)
{
    constexpr auto min64 = (std::numeric_limits<int64_t>::min)();
    constexpr auto max64 = (std::numeric_limits<int64_t>::max)();
    // min64 + max64 = -1 -> negative -> throws.
    EXPECT_THROW(m::math::add(min64, max64, uint64_t{}), std::overflow_error);
    // min64 + (min64 magnitude as positive is unrepresentable in int64, but the
    // sum with a large positive can still be valid):
    // min64 + max64 + 1 conceptually = 0; do it as two operands:
    EXPECT_EQ(m::math::add(int64_t{min64 + 1}, max64, uint64_t{}), 0u);
}

// ============================================================================
// subtract
// ============================================================================

TEST(SignedSignedToUnsigned, SubtractBasic)
{
    EXPECT_EQ(m::math::subtract(int32_t{50}, int32_t{8}, uint32_t{}), 42u);
    EXPECT_EQ(m::math::subtract(int32_t{8}, int32_t{8}, uint32_t{}), 0u);
}

TEST(SignedSignedToUnsigned, SubtractNegativeResultThrows)
{
    EXPECT_THROW(m::math::subtract(int32_t{8}, int32_t{50}, uint32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::subtract(int32_t{-5}, int32_t{1}, uint32_t{}), std::overflow_error);
}

TEST(SignedSignedToUnsigned, SubtractMinusNegativeIsAddition)
{
    EXPECT_EQ(m::math::subtract(int32_t{100}, int32_t{-50}, uint32_t{}), 150u);
    EXPECT_EQ(m::math::subtract(int32_t{0}, int32_t{-42}, uint32_t{}), 42u);
}

TEST(SignedSignedToUnsigned, SubtractLargeMagnitudeSpansBeyondIntmax)
{
    constexpr auto max64 = (std::numeric_limits<int64_t>::max)();
    constexpr auto min64 = (std::numeric_limits<int64_t>::min)();
    // max64 - min64 = 2^64 - 1, which fits exactly in uint64_t.
    EXPECT_EQ(m::math::subtract(max64, min64, uint64_t{}),
              (std::numeric_limits<uint64_t>::max)());
}

TEST(SignedSignedToUnsigned, SubtractBothNegative)
{
    // -10 - (-30) = 20.
    EXPECT_EQ(m::math::subtract(int32_t{-10}, int32_t{-30}, uint32_t{}), 20u);
    // -30 - (-10) = -20 -> throws.
    EXPECT_THROW(m::math::subtract(int32_t{-30}, int32_t{-10}, uint32_t{}), std::overflow_error);
}

// ============================================================================
// multiply
// ============================================================================

TEST(SignedSignedToUnsigned, MultiplyBasic)
{
    EXPECT_EQ(m::math::multiply(int32_t{6}, int32_t{7}, uint32_t{}), 42u);
    EXPECT_EQ(m::math::multiply(int32_t{0}, int32_t{12345}, uint32_t{}), 0u);
}

TEST(SignedSignedToUnsigned, MultiplyTwoNegativesIsPositive)
{
    EXPECT_EQ(m::math::multiply(int32_t{-6}, int32_t{-7}, uint32_t{}), 42u);
}

TEST(SignedSignedToUnsigned, MultiplyOppositeSignsThrows)
{
    EXPECT_THROW(m::math::multiply(int32_t{-6}, int32_t{7}, uint32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::multiply(int32_t{6}, int32_t{-7}, uint32_t{}), std::overflow_error);
}

TEST(SignedSignedToUnsigned, MultiplyOverflowThrows)
{
    constexpr auto max64 = (std::numeric_limits<int64_t>::max)();
    EXPECT_THROW(m::math::multiply(max64, max64, uint64_t{}), std::overflow_error);
}

TEST(SignedSignedToUnsigned, MultiplyNarrowResultOverflow)
{
    EXPECT_THROW(m::math::multiply(int32_t{20}, int32_t{20}, uint8_t{}), std::overflow_error);
    EXPECT_EQ(m::math::multiply(int32_t{15}, int32_t{17}, uint8_t{}), uint8_t{255});
}

// ============================================================================
// divide
// ============================================================================

TEST(SignedSignedToUnsigned, DivideBasic)
{
    EXPECT_EQ(m::math::divide(int32_t{84}, int32_t{2}, uint32_t{}), 42u);
    EXPECT_EQ(m::math::divide(int32_t{0}, int32_t{5}, uint32_t{}), 0u);
}

TEST(SignedSignedToUnsigned, DivideTwoNegativesIsPositive)
{
    EXPECT_EQ(m::math::divide(int32_t{-84}, int32_t{-2}, uint32_t{}), 42u);
}

TEST(SignedSignedToUnsigned, DivideByZeroThrows)
{
    EXPECT_THROW(m::math::divide(int32_t{1}, int32_t{0}, uint32_t{}), std::overflow_error);
}

TEST(SignedSignedToUnsigned, DivideOppositeSignsNonZeroThrows)
{
    EXPECT_THROW(m::math::divide(int32_t{-84}, int32_t{2}, uint32_t{}), std::overflow_error);
    EXPECT_THROW(m::math::divide(int32_t{84}, int32_t{-2}, uint32_t{}), std::overflow_error);
}

TEST(SignedSignedToUnsigned, DivideOppositeSignsTruncatingToZeroIsOk)
{
    // -3 / 5 truncates toward zero to 0, which is representable.
    EXPECT_EQ(m::math::divide(int32_t{-3}, int32_t{5}, uint32_t{}), 0u);
    EXPECT_EQ(m::math::divide(int32_t{3}, int32_t{-5}, uint32_t{}), 0u);
}

TEST(SignedSignedToUnsigned, DivideMostNegativeNumerator)
{
    constexpr auto min64 = (std::numeric_limits<int64_t>::min)();
    // min64 / -1 = 2^63, which fits in uint64_t (unlike the signed-result case).
    EXPECT_EQ(m::math::divide(min64, int64_t{-1}, uint64_t{}),
              static_cast<uint64_t>((std::numeric_limits<int64_t>::max)()) + 1);
}
