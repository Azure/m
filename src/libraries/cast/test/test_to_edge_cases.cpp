// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>
#include <m/cast/to.h>
#include <limits>
#include <type_traits>

// Comprehensive edge case tests for m::to<> integral conversions
// Tests all combinations of signed/unsigned, narrow/wide conversions

namespace
{
    // Test successful widening conversions (always safe)
    template <typename From, typename To>
        requires(std::integral<From> && std::integral<To> &&
                 (std::signed_integral<From> == std::signed_integral<To>) &&
                 (sizeof(To) >= sizeof(From)))
    void test_widening_conversion()
    {
        // Min value
        EXPECT_EQ(m::to<To>(std::numeric_limits<From>::min()), 
                  static_cast<To>(std::numeric_limits<From>::min()));
        
        // Max value
        EXPECT_EQ(m::to<To>(std::numeric_limits<From>::max()), 
                  static_cast<To>(std::numeric_limits<From>::max()));
        
        // Zero
        EXPECT_EQ(m::to<To>(From{0}), To{0});
        
        // One
        EXPECT_EQ(m::to<To>(From{1}), To{1});
        
        if constexpr (std::signed_integral<From>)
        {
            EXPECT_EQ(m::to<To>(From{-1}), To{-1});
        }
    }

    // Test narrowing conversions with values in range (should succeed)
    template <typename From, typename To>
        requires(std::integral<From> && std::integral<To> &&
                 (std::signed_integral<From> == std::signed_integral<To>) &&
                 (sizeof(To) < sizeof(From)))
    void test_narrowing_in_range()
    {
        // Values within To's range should succeed
        EXPECT_EQ(m::to<To>(From{0}), To{0});
        EXPECT_EQ(m::to<To>(From{1}), To{1});
        
        // Max of To should succeed
        EXPECT_EQ(m::to<To>(static_cast<From>(std::numeric_limits<To>::max())), 
                  std::numeric_limits<To>::max());
        
        // Min of To should succeed
        EXPECT_EQ(m::to<To>(static_cast<From>(std::numeric_limits<To>::min())), 
                  std::numeric_limits<To>::min());
    }

    // Test narrowing conversions with values out of range (should throw)
    template <typename From, typename To>
        requires(std::integral<From> && std::integral<To> &&
                 (std::signed_integral<From> == std::signed_integral<To>) &&
                 (sizeof(To) < sizeof(From)))
    void test_narrowing_overflow()
    {
        // Max of From should throw (exceeds To's max)
        EXPECT_THROW(m::to<To>(std::numeric_limits<From>::max()), std::overflow_error);
        
        if constexpr (std::signed_integral<From>)
        {
            // Min of From should throw (below To's min)
            EXPECT_THROW(m::to<To>(std::numeric_limits<From>::min()), std::overflow_error);
        }
        
        // Just beyond To's max
        if constexpr (std::numeric_limits<To>::max() < std::numeric_limits<From>::max())
        {
            EXPECT_THROW(m::to<To>(static_cast<From>(std::numeric_limits<To>::max()) + From{1}), 
                        std::overflow_error);
        }
        
        // Just below To's min
        if constexpr (std::signed_integral<To> && 
                      std::numeric_limits<To>::min() > std::numeric_limits<From>::min())
        {
            EXPECT_THROW(m::to<To>(static_cast<From>(std::numeric_limits<To>::min()) - From{1}), 
                        std::overflow_error);
        }
    }

    // Test signed to unsigned conversions
    template <typename From, typename To>
        requires(std::signed_integral<From> && std::unsigned_integral<To>)
    void test_signed_to_unsigned()
    {
        // Negative values should always throw
        EXPECT_THROW(m::to<To>(From{-1}), std::overflow_error);
        EXPECT_THROW(m::to<To>(std::numeric_limits<From>::min()), std::overflow_error);
        
        // Zero and positive values in range should succeed
        EXPECT_EQ(m::to<To>(From{0}), To{0});
        EXPECT_EQ(m::to<To>(From{1}), To{1});
        
        // Test overflow for positive values
        if constexpr (sizeof(From) >= sizeof(To))
        {
            // Large positive From values may exceed To's max
            if (static_cast<std::make_unsigned_t<From>>(std::numeric_limits<From>::max()) > 
                std::numeric_limits<To>::max())
            {
                EXPECT_THROW(m::to<To>(std::numeric_limits<From>::max()), std::overflow_error);
            }
        }
    }

    // Test unsigned to signed conversions
    template <typename From, typename To>
        requires(std::unsigned_integral<From> && std::signed_integral<To>)
    void test_unsigned_to_signed()
    {
        // Zero should always succeed
        EXPECT_EQ(m::to<To>(From{0}), To{0});

        // Values within To's positive range should succeed
        if constexpr (sizeof(From) < sizeof(To))
        {
            // All From values fit in To
            EXPECT_EQ(m::to<To>(std::numeric_limits<From>::max()), 
                     static_cast<To>(std::numeric_limits<From>::max()));
        }
        else
        {
            // Same size or larger: From's max exceeds To's max
            EXPECT_THROW(m::to<To>(std::numeric_limits<From>::max()), std::overflow_error);

            // Value at To's max should succeed
            EXPECT_EQ(m::to<To>(static_cast<From>(std::numeric_limits<To>::max())),
                     std::numeric_limits<To>::max());

            // Just beyond To's max should throw
            if (std::numeric_limits<From>::max() > static_cast<From>(std::numeric_limits<To>::max()))
            {
                EXPECT_THROW(m::to<To>(static_cast<From>(std::numeric_limits<To>::max()) + From{1}), 
                            std::overflow_error);
            }
        }
    }
} // namespace

// ============================================================================
// Signed -> Signed: Same size (always safe)
// ============================================================================
TEST(ToEdgeCases, Signed_int8_to_int8)   { test_widening_conversion<int8_t, int8_t>(); }
TEST(ToEdgeCases, Signed_int16_to_int16) { test_widening_conversion<int16_t, int16_t>(); }
TEST(ToEdgeCases, Signed_int32_to_int32) { test_widening_conversion<int32_t, int32_t>(); }
TEST(ToEdgeCases, Signed_int64_to_int64) { test_widening_conversion<int64_t, int64_t>(); }

// ============================================================================
// Signed -> Signed: Widening (always safe)
// ============================================================================
TEST(ToEdgeCases, Signed_int8_to_int16)  { test_widening_conversion<int8_t, int16_t>(); }
TEST(ToEdgeCases, Signed_int8_to_int32)  { test_widening_conversion<int8_t, int32_t>(); }
TEST(ToEdgeCases, Signed_int8_to_int64)  { test_widening_conversion<int8_t, int64_t>(); }
TEST(ToEdgeCases, Signed_int16_to_int32) { test_widening_conversion<int16_t, int32_t>(); }
TEST(ToEdgeCases, Signed_int16_to_int64) { test_widening_conversion<int16_t, int64_t>(); }
TEST(ToEdgeCases, Signed_int32_to_int64) { test_widening_conversion<int32_t, int64_t>(); }

// ============================================================================
// Signed -> Signed: Narrowing (overflow detection)
// ============================================================================
TEST(ToEdgeCases, Signed_int16_to_int8_InRange)  { test_narrowing_in_range<int16_t, int8_t>(); }
TEST(ToEdgeCases, Signed_int16_to_int8_Overflow) { test_narrowing_overflow<int16_t, int8_t>(); }
TEST(ToEdgeCases, Signed_int32_to_int8_InRange)  { test_narrowing_in_range<int32_t, int8_t>(); }
TEST(ToEdgeCases, Signed_int32_to_int8_Overflow) { test_narrowing_overflow<int32_t, int8_t>(); }
TEST(ToEdgeCases, Signed_int32_to_int16_InRange)  { test_narrowing_in_range<int32_t, int16_t>(); }
TEST(ToEdgeCases, Signed_int32_to_int16_Overflow) { test_narrowing_overflow<int32_t, int16_t>(); }
TEST(ToEdgeCases, Signed_int64_to_int8_InRange)  { test_narrowing_in_range<int64_t, int8_t>(); }
TEST(ToEdgeCases, Signed_int64_to_int8_Overflow) { test_narrowing_overflow<int64_t, int8_t>(); }
TEST(ToEdgeCases, Signed_int64_to_int16_InRange)  { test_narrowing_in_range<int64_t, int16_t>(); }
TEST(ToEdgeCases, Signed_int64_to_int16_Overflow) { test_narrowing_overflow<int64_t, int16_t>(); }
TEST(ToEdgeCases, Signed_int64_to_int32_InRange)  { test_narrowing_in_range<int64_t, int32_t>(); }
TEST(ToEdgeCases, Signed_int64_to_int32_Overflow) { test_narrowing_overflow<int64_t, int32_t>(); }

// ============================================================================
// Unsigned -> Unsigned: Same size (always safe)
// ============================================================================
TEST(ToEdgeCases, Unsigned_uint8_to_uint8)   { test_widening_conversion<uint8_t, uint8_t>(); }
TEST(ToEdgeCases, Unsigned_uint16_to_uint16) { test_widening_conversion<uint16_t, uint16_t>(); }
TEST(ToEdgeCases, Unsigned_uint32_to_uint32) { test_widening_conversion<uint32_t, uint32_t>(); }
TEST(ToEdgeCases, Unsigned_uint64_to_uint64) { test_widening_conversion<uint64_t, uint64_t>(); }

// ============================================================================
// Unsigned -> Unsigned: Widening (always safe)
// ============================================================================
TEST(ToEdgeCases, Unsigned_uint8_to_uint16)  { test_widening_conversion<uint8_t, uint16_t>(); }
TEST(ToEdgeCases, Unsigned_uint8_to_uint32)  { test_widening_conversion<uint8_t, uint32_t>(); }
TEST(ToEdgeCases, Unsigned_uint8_to_uint64)  { test_widening_conversion<uint8_t, uint64_t>(); }
TEST(ToEdgeCases, Unsigned_uint16_to_uint32) { test_widening_conversion<uint16_t, uint32_t>(); }
TEST(ToEdgeCases, Unsigned_uint16_to_uint64) { test_widening_conversion<uint16_t, uint64_t>(); }
TEST(ToEdgeCases, Unsigned_uint32_to_uint64) { test_widening_conversion<uint32_t, uint64_t>(); }

// ============================================================================
// Unsigned -> Unsigned: Narrowing (overflow detection)
// ============================================================================
TEST(ToEdgeCases, Unsigned_uint16_to_uint8_InRange)  { test_narrowing_in_range<uint16_t, uint8_t>(); }
TEST(ToEdgeCases, Unsigned_uint16_to_uint8_Overflow) { test_narrowing_overflow<uint16_t, uint8_t>(); }
TEST(ToEdgeCases, Unsigned_uint32_to_uint8_InRange)  { test_narrowing_in_range<uint32_t, uint8_t>(); }
TEST(ToEdgeCases, Unsigned_uint32_to_uint8_Overflow) { test_narrowing_overflow<uint32_t, uint8_t>(); }
TEST(ToEdgeCases, Unsigned_uint32_to_uint16_InRange)  { test_narrowing_in_range<uint32_t, uint16_t>(); }
TEST(ToEdgeCases, Unsigned_uint32_to_uint16_Overflow) { test_narrowing_overflow<uint32_t, uint16_t>(); }
TEST(ToEdgeCases, Unsigned_uint64_to_uint8_InRange)  { test_narrowing_in_range<uint64_t, uint8_t>(); }
TEST(ToEdgeCases, Unsigned_uint64_to_uint8_Overflow) { test_narrowing_overflow<uint64_t, uint8_t>(); }
TEST(ToEdgeCases, Unsigned_uint64_to_uint16_InRange)  { test_narrowing_in_range<uint64_t, uint16_t>(); }
TEST(ToEdgeCases, Unsigned_uint64_to_uint16_Overflow) { test_narrowing_overflow<uint64_t, uint16_t>(); }
TEST(ToEdgeCases, Unsigned_uint64_to_uint32_InRange)  { test_narrowing_in_range<uint64_t, uint32_t>(); }
TEST(ToEdgeCases, Unsigned_uint64_to_uint32_Overflow) { test_narrowing_overflow<uint64_t, uint32_t>(); }

// ============================================================================
// Signed -> Unsigned (negative values always throw)
// ============================================================================
TEST(ToEdgeCases, SignedToUnsigned_int8_to_uint8)   { test_signed_to_unsigned<int8_t, uint8_t>(); }
TEST(ToEdgeCases, SignedToUnsigned_int8_to_uint16)  { test_signed_to_unsigned<int8_t, uint16_t>(); }
TEST(ToEdgeCases, SignedToUnsigned_int8_to_uint32)  { test_signed_to_unsigned<int8_t, uint32_t>(); }
TEST(ToEdgeCases, SignedToUnsigned_int8_to_uint64)  { test_signed_to_unsigned<int8_t, uint64_t>(); }
TEST(ToEdgeCases, SignedToUnsigned_int16_to_uint8)  { test_signed_to_unsigned<int16_t, uint8_t>(); }
TEST(ToEdgeCases, SignedToUnsigned_int16_to_uint16) { test_signed_to_unsigned<int16_t, uint16_t>(); }
TEST(ToEdgeCases, SignedToUnsigned_int16_to_uint32) { test_signed_to_unsigned<int16_t, uint32_t>(); }
TEST(ToEdgeCases, SignedToUnsigned_int16_to_uint64) { test_signed_to_unsigned<int16_t, uint64_t>(); }
TEST(ToEdgeCases, SignedToUnsigned_int32_to_uint8)  { test_signed_to_unsigned<int32_t, uint8_t>(); }
TEST(ToEdgeCases, SignedToUnsigned_int32_to_uint16) { test_signed_to_unsigned<int32_t, uint16_t>(); }
TEST(ToEdgeCases, SignedToUnsigned_int32_to_uint32) { test_signed_to_unsigned<int32_t, uint32_t>(); }
TEST(ToEdgeCases, SignedToUnsigned_int32_to_uint64) { test_signed_to_unsigned<int32_t, uint64_t>(); }
TEST(ToEdgeCases, SignedToUnsigned_int64_to_uint8)  { test_signed_to_unsigned<int64_t, uint8_t>(); }
TEST(ToEdgeCases, SignedToUnsigned_int64_to_uint16) { test_signed_to_unsigned<int64_t, uint16_t>(); }
TEST(ToEdgeCases, SignedToUnsigned_int64_to_uint32) { test_signed_to_unsigned<int64_t, uint32_t>(); }
TEST(ToEdgeCases, SignedToUnsigned_int64_to_uint64) { test_signed_to_unsigned<int64_t, uint64_t>(); }

// ============================================================================
// Unsigned -> Signed (large values may overflow)
// ============================================================================
TEST(ToEdgeCases, UnsignedToSigned_uint8_to_int8)   { test_unsigned_to_signed<uint8_t, int8_t>(); }
TEST(ToEdgeCases, UnsignedToSigned_uint8_to_int16)  { test_unsigned_to_signed<uint8_t, int16_t>(); }
TEST(ToEdgeCases, UnsignedToSigned_uint8_to_int32)  { test_unsigned_to_signed<uint8_t, int32_t>(); }
TEST(ToEdgeCases, UnsignedToSigned_uint8_to_int64)  { test_unsigned_to_signed<uint8_t, int64_t>(); }
TEST(ToEdgeCases, UnsignedToSigned_uint16_to_int8)  { test_unsigned_to_signed<uint16_t, int8_t>(); }
TEST(ToEdgeCases, UnsignedToSigned_uint16_to_int16) { test_unsigned_to_signed<uint16_t, int16_t>(); }
TEST(ToEdgeCases, UnsignedToSigned_uint16_to_int32) { test_unsigned_to_signed<uint16_t, int32_t>(); }
TEST(ToEdgeCases, UnsignedToSigned_uint16_to_int64) { test_unsigned_to_signed<uint16_t, int64_t>(); }
TEST(ToEdgeCases, UnsignedToSigned_uint32_to_int8)  { test_unsigned_to_signed<uint32_t, int8_t>(); }
TEST(ToEdgeCases, UnsignedToSigned_uint32_to_int16) { test_unsigned_to_signed<uint32_t, int16_t>(); }
TEST(ToEdgeCases, UnsignedToSigned_uint32_to_int32) { test_unsigned_to_signed<uint32_t, int32_t>(); }
TEST(ToEdgeCases, UnsignedToSigned_uint32_to_int64) { test_unsigned_to_signed<uint32_t, int64_t>(); }
TEST(ToEdgeCases, UnsignedToSigned_uint64_to_int8)  { test_unsigned_to_signed<uint64_t, int8_t>(); }
TEST(ToEdgeCases, UnsignedToSigned_uint64_to_int16) { test_unsigned_to_signed<uint64_t, int16_t>(); }
TEST(ToEdgeCases, UnsignedToSigned_uint64_to_int32) { test_unsigned_to_signed<uint64_t, int32_t>(); }
TEST(ToEdgeCases, UnsignedToSigned_uint64_to_int64) { test_unsigned_to_signed<uint64_t, int64_t>(); }
