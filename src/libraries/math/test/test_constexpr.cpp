// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>

#include <m/math/math.h>

//
// The public math operations are all declared constexpr. These tests verify
// that they are genuinely usable in a constant-expression context (and produce
// the correct values there), which is a behavioral guarantee that the
// value-based runtime tests do not exercise.
//

// ============================================================================
// add
// ============================================================================

static_assert(m::math::add(uint32_t{10}, uint32_t{20}, uint32_t{}) == 30u);
static_assert(m::math::add(int32_t{-5}, int32_t{12}, int32_t{}) == 7);
static_assert(m::math::add(uint32_t{100}, int32_t{-40}, uint32_t{}) == 60u);
static_assert(m::math::add(int32_t{-40}, uint32_t{100}, int32_t{}) == 60);
static_assert(m::math::add(int32_t{-5}, int32_t{12}, uint32_t{}) == 7u);
static_assert(m::math::add(uint8_t{200}, uint8_t{200}, uint16_t{}) == 400u);

// ============================================================================
// subtract
// ============================================================================

static_assert(m::math::subtract(uint32_t{50}, uint32_t{8}, uint32_t{}) == 42u);
static_assert(m::math::subtract(int32_t{5}, int32_t{8}, int32_t{}) == -3);
static_assert(m::math::subtract(uint32_t{100}, int32_t{-50}, uint32_t{}) == 150u);
static_assert(m::math::subtract(int32_t{50}, int32_t{8}, uint32_t{}) == 42u);

// ============================================================================
// multiply
// ============================================================================

static_assert(m::math::multiply(uint32_t{6}, uint32_t{7}, uint32_t{}) == 42u);
static_assert(m::math::multiply(int32_t{-4}, int32_t{5}, int32_t{}) == -20);
static_assert(m::math::multiply(int32_t{-6}, int32_t{-7}, uint32_t{}) == 42u);
static_assert(m::math::multiply(uint32_t{0}, int32_t{-9}, uint32_t{}) == 0u);

// ============================================================================
// divide
// ============================================================================

static_assert(m::math::divide(uint32_t{20}, uint32_t{4}, uint32_t{}) == 5u);
static_assert(m::math::divide(int32_t{-20}, int32_t{4}, int32_t{}) == -5);
static_assert(m::math::divide(uint32_t{100}, int32_t{-10}, int32_t{}) == -10);

// ============================================================================
// negate
// ============================================================================

static_assert(m::math::negate(int32_t{7}, int32_t{}) == -7);
static_assert(m::math::negate(int32_t{-7}, int32_t{}) == 7);
static_assert(m::math::negate(int32_t{-7}, uint32_t{}) == 7u);
static_assert(m::math::negate(uint32_t{0}, int32_t{}) == 0);

// ============================================================================
// Runtime mirrors so the constant-expression guarantees are reported by the
// test runner as an executed test as well.
// ============================================================================

TEST(ConstexprEvaluation, ResultsUsableInConstantExpressions)
{
    // Each of these is a compile-time constant; capturing them in constexpr
    // locals re-confirms constant-expression usability at this point too.
    constexpr auto sum  = m::math::add(int32_t{-5}, int32_t{12}, int32_t{});
    constexpr auto diff = m::math::subtract(int32_t{5}, int32_t{8}, int32_t{});
    constexpr auto prod = m::math::multiply(int32_t{-4}, int32_t{5}, int32_t{});
    constexpr auto quot = m::math::divide(int32_t{-20}, int32_t{4}, int32_t{});
    constexpr auto neg  = m::math::negate(int32_t{7}, int32_t{});

    EXPECT_EQ(sum, 7);
    EXPECT_EQ(diff, -3);
    EXPECT_EQ(prod, -20);
    EXPECT_EQ(quot, -5);
    EXPECT_EQ(neg, -7);
}
