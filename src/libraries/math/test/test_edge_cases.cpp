// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>

#include <m/math/math.h>

//
// Additional edge-case coverage that the per-operation test files do not
// exercise:
//
//   * producing exactly the most-negative representable result from a
//     mixed-sign add/subtract (the "magnitude == |ResultT::min|" branches),
//   * the 64-bit INT_MIN special-case branches of the mixed-sign multiply
//     specializations (existing tests only reach these with 32-bit inputs,
//     which do not take the intmax_t::min code paths),
//   * multiplication that narrows to a smaller result type, and
//   * negate boundaries at the full 64-bit width.
//

// ============================================================================
// Mixed-sign add: most-negative result boundary
// (signed + unsigned -> signed) and (unsigned + signed -> signed)
// ============================================================================

TEST(MixedSignMostNegative, SignedUnsignedAddProducesResultMin)
{
    constexpr auto min8 = (std::numeric_limits<int8_t>::min)(); // -128

    // -200 + 72 = -128 = INT8_MIN: hits the "magnitude == |min|" branch.
    EXPECT_EQ(m::math::add(int16_t{-200}, uint16_t{72}, int8_t{}), min8);

    // -201 + 72 = -129: one below INT8_MIN, must overflow.
    EXPECT_THROW(m::math::add(int16_t{-201}, uint16_t{72}, int8_t{}), std::overflow_error);

    // -100 + 30 = -70: ordinary negative result just inside range.
    EXPECT_EQ(m::math::add(int16_t{-100}, uint16_t{30}, int8_t{}), int8_t{-70});
}

TEST(MixedSignMostNegative, UnsignedSignedAddProducesResultMin)
{
    constexpr auto min8 = (std::numeric_limits<int8_t>::min)(); // -128

    // 72 + (-200) = -128 = INT8_MIN.
    EXPECT_EQ(m::math::add(uint16_t{72}, int16_t{-200}, int8_t{}), min8);

    // 72 + (-201) = -129: overflow.
    EXPECT_THROW(m::math::add(uint16_t{72}, int16_t{-201}, int8_t{}), std::overflow_error);
}

// ============================================================================
// Mixed-sign subtract: most-negative result boundary
// (signed - unsigned -> signed)
// ============================================================================

TEST(MixedSignMostNegative, SignedUnsignedSubtractProducesResultMin)
{
    constexpr auto min8 = (std::numeric_limits<int8_t>::min)(); // -128

    // -28 - 100 = -128 = INT8_MIN.
    EXPECT_EQ(m::math::subtract(int16_t{-28}, uint16_t{100}, int8_t{}), min8);

    // -29 - 100 = -129: overflow.
    EXPECT_THROW(m::math::subtract(int16_t{-29}, uint16_t{100}, int8_t{}), std::overflow_error);
}

// ============================================================================
// 64-bit INT_MIN special-case branches of the mixed-sign multiply helpers.
// The result type matches the 64-bit input width so the most-negative product
// is representable; existing tests only use 32-bit inputs and therefore never
// take the intmax_t::min branch.
// ============================================================================

TEST(IntMin64Multiply, SignedUnsignedToSigned)
{
    constexpr auto min64 = (std::numeric_limits<int64_t>::min)();

    // INT64_MIN * 1 = INT64_MIN: the product equals |INT64_MIN| exactly.
    EXPECT_EQ(m::math::multiply(min64, uint64_t{1}, int64_t{}), min64);

    // INT64_MIN * 2 overflows the 64-bit signed range.
    EXPECT_THROW(m::math::multiply(min64, uint64_t{2}, int64_t{}), std::overflow_error);

    // INT64_MIN * 0 = 0 (early-out before the special case).
    EXPECT_EQ(m::math::multiply(min64, uint64_t{0}, int64_t{}), 0);
}

TEST(IntMin64Multiply, UnsignedSignedToSigned)
{
    constexpr auto min64 = (std::numeric_limits<int64_t>::min)();

    // 1 * INT64_MIN = INT64_MIN.
    EXPECT_EQ(m::math::multiply(uint64_t{1}, min64, int64_t{}), min64);

    // 2 * INT64_MIN overflows.
    EXPECT_THROW(m::math::multiply(uint64_t{2}, min64, int64_t{}), std::overflow_error);

    // 0 * INT64_MIN = 0.
    EXPECT_EQ(m::math::multiply(uint64_t{0}, min64, int64_t{}), 0);
}

TEST(IntMin64Multiply, SignedSignedToSigned)
{
    constexpr auto min64 = (std::numeric_limits<int64_t>::min)();

    // INT64_MIN * 1 = INT64_MIN exercises the abs(INT_MIN) handling on the
    // left operand of the signed*signed helper.
    EXPECT_EQ(m::math::multiply(min64, int64_t{1}, int64_t{}), min64);

    // 1 * INT64_MIN = INT64_MIN exercises it on the right operand.
    EXPECT_EQ(m::math::multiply(int64_t{1}, min64, int64_t{}), min64);

    // INT64_MIN * -1 overflows (result would be INT64_MAX + 1).
    EXPECT_THROW(m::math::multiply(min64, int64_t{-1}, int64_t{}), std::overflow_error);
}

// ============================================================================
// Multiplication that narrows to a smaller result type.
// ============================================================================

TEST(MultiplyNarrowing, UnsignedToUnsigned)
{
    // 15 * 17 = 255 fits exactly in uint8_t.
    EXPECT_EQ(m::math::multiply(uint32_t{15}, uint32_t{17}, uint8_t{}), uint8_t{255});

    // 20 * 20 = 400 does not fit in uint8_t.
    EXPECT_THROW(m::math::multiply(uint32_t{20}, uint32_t{20}, uint8_t{}), std::overflow_error);
}

TEST(MultiplyNarrowing, SignedToSignedPositive)
{
    // 12 * 10 = 120 fits in int8_t.
    EXPECT_EQ(m::math::multiply(int32_t{12}, int32_t{10}, int8_t{}), int8_t{120});

    // 13 * 10 = 130 exceeds INT8_MAX (127).
    EXPECT_THROW(m::math::multiply(int32_t{13}, int32_t{10}, int8_t{}), std::overflow_error);
}

TEST(MultiplyNarrowing, SignedToSignedNegative)
{
    constexpr auto min8 = (std::numeric_limits<int8_t>::min)(); // -128

    // -12 * 10 = -120 fits in int8_t.
    EXPECT_EQ(m::math::multiply(int32_t{-12}, int32_t{10}, int8_t{}), int8_t{-120});

    // -16 * 8 = -128 = INT8_MIN fits exactly.
    EXPECT_EQ(m::math::multiply(int32_t{-16}, int32_t{8}, int8_t{}), min8);

    // -16 * 9 = -144 is below INT8_MIN.
    EXPECT_THROW(m::math::multiply(int32_t{-16}, int32_t{9}, int8_t{}), std::overflow_error);
}

// ============================================================================
// 64-bit divide: negative dividend via the (signed / unsigned -> signed)
// general branch (non-min, but with a 64-bit-wide magnitude).
// ============================================================================

TEST(Divide64, SignedUnsignedNegativeDividend)
{
    constexpr auto max64 = (std::numeric_limits<int64_t>::max)();

    // -max64 / 1 = -max64 (largest-magnitude representable negative result).
    EXPECT_EQ(m::math::divide(int64_t{-max64}, uint64_t{1}, int64_t{}), -max64);

    // -1000000000000 / 1000000 = -1000000.
    EXPECT_EQ(m::math::divide(int64_t{-1000000000000LL}, uint64_t{1000000}, int64_t{}),
              int64_t{-1000000});

    // Truncation toward zero for a small-magnitude negative quotient.
    EXPECT_EQ(m::math::divide(int64_t{-7}, uint64_t{2}, int64_t{}), int64_t{-3});
}

// ============================================================================
// Divide producing exactly the most-negative result. Both the
// (signed / unsigned) -> signed and (unsigned / signed) -> signed helpers
// must yield ResultT::min() when the magnitude of the quotient equals
// |ResultT::min()|, rather than rejecting it as overflow.
// ============================================================================

TEST(DivideMostNegativeResult, SignedUnsignedToSignedAtMin)
{
    constexpr auto min64 = (std::numeric_limits<int64_t>::min)();

    // INT64_MIN / 1 = INT64_MIN: magnitude is 2^63 == |INT64_MIN|.
    EXPECT_EQ(m::math::divide(min64, uint64_t{1}, int64_t{}), min64);

    // INT64_MIN / 2 = -2^62, comfortably in range.
    EXPECT_EQ(m::math::divide(min64, uint64_t{2}, int64_t{}), min64 / 2);

    // Most-negative result narrowed to int8_t: -128 / 1 = -128 is OK,
    // but a magnitude one larger must overflow.
    EXPECT_EQ(m::math::divide(int16_t{-128}, uint16_t{1}, int8_t{}),
              (std::numeric_limits<int8_t>::min)());
    EXPECT_THROW(m::math::divide(int16_t{-129}, uint16_t{1}, int8_t{}), std::overflow_error);
}

TEST(DivideMostNegativeResult, UnsignedSignedToSignedAtMin)
{
    constexpr auto min64 = (std::numeric_limits<int64_t>::min)();
    constexpr uint64_t abs_min = uint64_t{1} << 63; // |INT64_MIN| = 2^63

    // 2^63 / -1 = -2^63 = INT64_MIN: hits the divisor == RightT::min-adjacent
    // negative branch and produces the most-negative result.
    EXPECT_EQ(m::math::divide(abs_min, int64_t{-1}, int64_t{}), min64);

    // 2^63 / INT64_MIN = -1 (divisor is RightT::min special case).
    EXPECT_EQ(m::math::divide(abs_min, min64, int64_t{}), int64_t{-1});

    // Narrowed: 128 / -1 = -128 = INT8_MIN is OK; 129 / -1 = -129 overflows.
    EXPECT_EQ(m::math::divide(uint16_t{128}, int16_t{-1}, int8_t{}),
              (std::numeric_limits<int8_t>::min)());
    EXPECT_THROW(m::math::divide(uint16_t{129}, int16_t{-1}, int8_t{}), std::overflow_error);
}


// ============================================================================
// negate boundaries at full 64-bit width (unsigned -> signed and signed ->
// signed), which the existing negate tests only probe at narrower widths.
// ============================================================================

TEST(Negate64Boundary, UnsignedToSignedAtMin)
{
    constexpr auto min64        = (std::numeric_limits<int64_t>::min)();
    constexpr uint64_t abs_min  = uint64_t{1} << 63; // |INT64_MIN| = 2^63

    // Negating 2^63 yields exactly INT64_MIN.
    EXPECT_EQ(m::math::negate(abs_min, int64_t{}), min64);

    // One above the boundary cannot be represented.
    EXPECT_THROW(m::math::negate(abs_min + 1, int64_t{}), std::overflow_error);

    // Just inside the boundary.
    EXPECT_EQ(m::math::negate(abs_min - 1, int64_t{}), -(std::numeric_limits<int64_t>::max)());
}

TEST(Negate64Boundary, SignedToSignedAtMin)
{
    constexpr auto min64 = (std::numeric_limits<int64_t>::min)();
    constexpr auto max64 = (std::numeric_limits<int64_t>::max)();

    // INT64_MIN cannot be negated within int64_t.
    EXPECT_THROW(m::math::negate(min64, int64_t{}), std::overflow_error);

    // INT64_MAX negates to -INT64_MAX.
    EXPECT_EQ(m::math::negate(max64, int64_t{}), -max64);
}
