// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>

#include <m/math/math.h>

//
// Tests for intermediate value overflow edge cases
// These tests verify that when input types equal intermediate type sizes,
// overflow detection still works correctly in the intermediate computation itself
//

// ============================================================================
// Maximum-Sized Type Operations (uintmax_t/intmax_t as inputs)
// ============================================================================

TEST(IntermediateOverflow, UIntMaxAddition)
{
    // When inputs are uintmax_t, intermediate is also uintmax_t
    // Addition overflow must be detected in the intermediate type itself
    constexpr auto max = (std::numeric_limits<uintmax_t>::max)();
    
    // MAX + 1 should overflow in intermediate
    EXPECT_THROW(m::math::add(max, uintmax_t{1}, uintmax_t{}), std::overflow_error);
    
    // MAX + MAX should overflow in intermediate
    EXPECT_THROW(m::math::add(max, max, uintmax_t{}), std::overflow_error);
    
    // Large + large should overflow
    uintmax_t large = max / 2 + 1;
    EXPECT_THROW(m::math::add(large, large, uintmax_t{}), std::overflow_error);
}

TEST(IntermediateOverflow, UIntMaxMultiplication)
{
    // Multiplication overflow detection via division-back
    constexpr auto max = (std::numeric_limits<uintmax_t>::max)();
    
    // MAX * 2 should overflow in intermediate
    EXPECT_THROW(m::math::multiply(max, uintmax_t{2}, uintmax_t{}), std::overflow_error);
    
    // MAX * MAX should overflow in intermediate
    EXPECT_THROW(m::math::multiply(max, max, uintmax_t{}), std::overflow_error);
    
    // Square root of max squared should overflow
    uintmax_t sqrt_max = uintmax_t{1} << (sizeof(uintmax_t) * 4); // Approx sqrt(max)
    EXPECT_THROW(m::math::multiply(sqrt_max, sqrt_max, uintmax_t{}), std::overflow_error);
}

TEST(IntermediateOverflow, IntMaxAddition)
{
    // Signed addition with inputs at maximum size
    constexpr auto max = (std::numeric_limits<intmax_t>::max)();
    constexpr auto min = (std::numeric_limits<intmax_t>::min)();
    
    // MAX + 1 should overflow
    EXPECT_THROW(m::math::add(max, intmax_t{1}, intmax_t{}), std::overflow_error);
    
    // MAX + MAX should overflow
    EXPECT_THROW(m::math::add(max, max, intmax_t{}), std::overflow_error);
    
    // MIN + (-1) should overflow
    EXPECT_THROW(m::math::add(min, intmax_t{-1}, intmax_t{}), std::overflow_error);
    
    // MIN + MIN should overflow
    EXPECT_THROW(m::math::add(min, min, intmax_t{}), std::overflow_error);
}

TEST(IntermediateOverflow, IntMaxMultiplication)
{
    // Signed multiplication with inputs at maximum size
    constexpr auto max = (std::numeric_limits<intmax_t>::max)();
    constexpr auto min = (std::numeric_limits<intmax_t>::min)();
    
    // MAX * 2 should overflow
    EXPECT_THROW(m::math::multiply(max, intmax_t{2}, intmax_t{}), std::overflow_error);
    
    // MIN * 2 should overflow
    EXPECT_THROW(m::math::multiply(min, intmax_t{2}, intmax_t{}), std::overflow_error);
    
    // MIN * (-1) should overflow (special case)
    EXPECT_THROW(m::math::multiply(min, intmax_t{-1}, intmax_t{}), std::overflow_error);
    
    // MAX * MAX should overflow
    EXPECT_THROW(m::math::multiply(max, max, intmax_t{}), std::overflow_error);
}

// ============================================================================
// Narrowing from Intermediate to Smaller Result Type
// ============================================================================

TEST(IntermediateOverflow, LargeIntermediateToSmallResult)
{
    // Operation succeeds in intermediate, but result doesn't fit in target
    
    // uint32_t + uint32_t = large value, narrow to uint8_t
    EXPECT_THROW(m::math::add(uint32_t{200}, uint32_t{200}, uint8_t{}), std::overflow_error);
    
    // uint64_t + uint64_t = fits in uint64_t, doesn't fit in uint32_t
    uint64_t large = uint64_t{1} << 33; // Larger than uint32_t max
    EXPECT_THROW(m::math::add(large, uint64_t{1}, uint32_t{}), std::overflow_error);
}

TEST(IntermediateOverflow, SignedLargeIntermediateToSmallResult)
{
    // Operation succeeds in intermediate, but result doesn't fit in target
    
    // int32_t + int32_t = large value, narrow to int8_t
    EXPECT_THROW(m::math::add(int32_t{100}, int32_t{100}, int8_t{}), std::overflow_error);
    
    // int64_t + int64_t = fits in int64_t, doesn't fit in int32_t
    int64_t large = int64_t{1} << 32; // Larger than int32_t max
    EXPECT_THROW(m::math::add(large, int64_t{1}, int32_t{}), std::overflow_error);
}

// ============================================================================
// Widening from Small to Large with Successful Intermediate Computation
// ============================================================================

TEST(IntermediateOverflow, SmallToLargeSucceeds)
{
    // These should succeed because intermediate has room
    
    // uint8_t + uint8_t can overflow uint8_t but fit in uint16_t
    EXPECT_EQ(m::math::add(uint8_t{200}, uint8_t{200}, uint16_t{}), 400);
    
    // uint16_t + uint16_t can overflow uint16_t but fit in uint32_t
    EXPECT_EQ(m::math::add(uint16_t{40000}, uint16_t{40000}, uint32_t{}), 80000);
    
    // uint32_t + uint32_t can overflow uint32_t but fit in uint64_t
    uint32_t large32 = (std::numeric_limits<uint32_t>::max)() / 2 + 1;
    EXPECT_EQ(m::math::add(large32, large32, uint64_t{}), 
              static_cast<uint64_t>(large32) * 2);
}

TEST(IntermediateOverflow, SignedSmallToLargeSucceeds)
{
    // int8_t + int8_t can overflow int8_t but fit in int16_t
    EXPECT_EQ(m::math::add(int8_t{100}, int8_t{100}, int16_t{}), 200);
    
    // int16_t + int16_t can overflow int16_t but fit in int32_t
    EXPECT_EQ(m::math::add(int16_t{20000}, int16_t{20000}, int32_t{}), 40000);
    
    // int32_t + int32_t can overflow int32_t but fit in int64_t
    int32_t large32 = (std::numeric_limits<int32_t>::max)() / 2 + 1;
    EXPECT_EQ(m::math::add(large32, large32, int64_t{}), 
              static_cast<int64_t>(large32) * 2);
}

// ============================================================================
// Subtraction with Maximum-Sized Types
// ============================================================================

TEST(IntermediateOverflow, UIntMaxSubtraction)
{
    constexpr auto max = (std::numeric_limits<uintmax_t>::max)();
    
    // MAX - 0 = MAX (should work)
    EXPECT_EQ(m::math::subtract(max, uintmax_t{0}, uintmax_t{}), max);
    
    // 0 - 1 should overflow (negative result for unsigned)
    EXPECT_THROW(m::math::subtract(uintmax_t{0}, uintmax_t{1}, uintmax_t{}), std::overflow_error);
    
    // Small - large should overflow
    EXPECT_THROW(m::math::subtract(uintmax_t{100}, uintmax_t{200}, uintmax_t{}), std::overflow_error);
}

TEST(IntermediateOverflow, IntMaxSubtraction)
{
    constexpr auto max = (std::numeric_limits<intmax_t>::max)();
    constexpr auto min = (std::numeric_limits<intmax_t>::min)();
    
    // MAX - (-1) should overflow
    EXPECT_THROW(m::math::subtract(max, intmax_t{-1}, intmax_t{}), std::overflow_error);
    
    // MIN - 1 should overflow
    EXPECT_THROW(m::math::subtract(min, intmax_t{1}, intmax_t{}), std::overflow_error);
    
    // MAX - MIN should overflow (result > MAX)
    EXPECT_THROW(m::math::subtract(max, min, intmax_t{}), std::overflow_error);
}

// ============================================================================
// Division with Maximum-Sized Types
// ============================================================================

TEST(IntermediateOverflow, IntMaxDivision)
{
    constexpr auto min = (std::numeric_limits<intmax_t>::min)();
    
    // MIN / (-1) is the classic signed overflow case
    EXPECT_THROW(m::math::divide(min, intmax_t{-1}, intmax_t{}), std::overflow_error);
    
    // MIN / 1 = MIN (should work)
    EXPECT_EQ(m::math::divide(min, intmax_t{1}, intmax_t{}), min);
    
    // Division by zero
    EXPECT_THROW(m::math::divide(intmax_t{100}, intmax_t{0}, intmax_t{}), std::overflow_error);
}

// ============================================================================
// Mixed-Size Operations Where Intermediate Matters
// ============================================================================

TEST(IntermediateOverflow, MixedSizesIntermediateRoom)
{
    // uint8_t * uint8_t fits in uint16_t intermediate
    EXPECT_EQ(m::math::multiply(uint8_t{255}, uint8_t{255}, uint32_t{}), 65025u);
    
    // uint16_t * uint16_t fits in uint32_t intermediate  
    EXPECT_EQ(m::math::multiply(uint16_t{65535}, uint16_t{2}, uint32_t{}), 131070u);
    
    // uint32_t * uint32_t fits in uint64_t intermediate
    uint32_t val = (std::numeric_limits<uint32_t>::max)();
    EXPECT_NO_THROW(m::math::multiply(val, uint32_t{2}, uint64_t{}));
}

TEST(IntermediateOverflow, MixedSizesIntermediateOverflow)
{
    // uint64_t * uint64_t doesn't fit in uint64_t intermediate
    constexpr auto max64 = (std::numeric_limits<uint64_t>::max)();
    EXPECT_THROW(m::math::multiply(max64, uint64_t{2}, uint64_t{}), std::overflow_error);
    
    // Even with smaller result type, intermediate overflow should be detected
    EXPECT_THROW(m::math::multiply(max64, uint64_t{2}, uint32_t{}), std::overflow_error);
}
