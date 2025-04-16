// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include <m/cast/try_cast.h>

template <typename T>
void
trycast_test_signed_to_signed_same_type()
{
    T const least     = std::numeric_limits<T>::min();
    T const greatest  = std::numeric_limits<T>::max();
    T const minus_one = static_cast<T>(-1);
    T const zero      = static_cast<T>(0);
    T const one       = static_cast<T>(1);

    auto const r_least     = m::try_cast<T>(least);
    auto const r_greatest  = m::try_cast<T>(greatest);
    auto const r_minus_one = m::try_cast<T>(minus_one);
    auto const r_zero      = m::try_cast<T>(zero);
    auto const r_one       = m::try_cast<T>(one);

    EXPECT_EQ(m::try_cast<T>(least), least);
    EXPECT_EQ(m::try_cast<T>(greatest), greatest);
    EXPECT_EQ(m::try_cast<T>(minus_one), minus_one);
    EXPECT_EQ(m::try_cast<T>(zero), zero);
    EXPECT_EQ(m::try_cast<T>(one), one);
}

TEST(TryCastTests, SignedToSigned_SameType_int8_t)
{
    trycast_test_signed_to_signed_same_type<int8_t>();
}

TEST(TryCastTests, SignedToSigned_SameType_int16_t)
{
    trycast_test_signed_to_signed_same_type<int16_t>();
}

TEST(TryCastTests, SignedToSigned_SameType_int32_t)
{
    trycast_test_signed_to_signed_same_type<int32_t>();
}

TEST(TryCastTests, SignedToSigned_SameType_int64_t)
{
    trycast_test_signed_to_signed_same_type<int64_t>();
}

template <typename T>
void
trycast_test_unsigned_to_unsigned_same_type()
{
    T greatest = std::numeric_limits<T>::max();
    T mid      = greatest / 2;
    T zero     = static_cast<T>(0);
    T one      = static_cast<T>(1);

    EXPECT_EQ(m::try_cast<T>(greatest), greatest);
    EXPECT_EQ(m::try_cast<T>(mid), mid);
    EXPECT_EQ(m::try_cast<T>(zero), zero);
    EXPECT_EQ(m::try_cast<T>(one), one);
}

TEST(TryCastTests, UnsignedToUnsigned_SameType_uint8_t)
{
    trycast_test_unsigned_to_unsigned_same_type<uint8_t>();
}

TEST(TryCastTests, UnsignedToUnsigned_SameType_uint16_t)
{
    trycast_test_unsigned_to_unsigned_same_type<uint16_t>();
}

TEST(TryCastTests, UnsignedToUnsigned_SameType_uint32_t)
{
    trycast_test_unsigned_to_unsigned_same_type<uint32_t>();
}

TEST(TryCastTests, UnsignedToUnsigned_SameType_uint64_t)
{
    trycast_test_unsigned_to_unsigned_same_type<uint64_t>();
}

TEST(TryCastTests, size_t_max_to_uint_8_throw)
{
    constexpr auto x = std::numeric_limits<size_t>::max();
    EXPECT_THROW(auto y = m::try_cast<uint8_t>(x), std::overflow_error);
}

TEST(TryCastTests, uint8_t_max_plus_1_to_uint_8_throw)
{
    constexpr auto x = static_cast<size_t>(std::numeric_limits<uint8_t>::max()) + 1;
    EXPECT_THROW(auto y = m::try_cast<uint8_t>(x), std::overflow_error);
}
