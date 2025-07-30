// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#include <m/cast/cast.h>

//
// Testing m::cast<>() is kind of boring; it either compiles
// or it doesn't. But if it compiles, the value does have to
// be correct. So we will verify a number of cases.
//

template <typename T>
void
cast_test_signed_to_signed_same_type()
{
    T least     = std::numeric_limits<T>::min();
    T greatest  = std::numeric_limits<T>::max();
    T minus_one = static_cast<T>(-1);
    T zero      = static_cast<T>(0);
    T one       = static_cast<T>(1);

    EXPECT_EQ(m::cast<T>(least), least);
    EXPECT_EQ(m::cast<T>(greatest), greatest);
    EXPECT_EQ(m::cast<T>(minus_one), minus_one);
    EXPECT_EQ(m::cast<T>(zero), zero);
    EXPECT_EQ(m::cast<T>(one), one);
}

TEST(CastTests, SignedToSigned_SameType_int8_t) { cast_test_signed_to_signed_same_type<int8_t>(); }
TEST(CastTests, SignedToSigned_SameType_int16_t) { cast_test_signed_to_signed_same_type<int16_t>(); }
TEST(CastTests, SignedToSigned_SameType_int32_t) { cast_test_signed_to_signed_same_type<int32_t>(); }
TEST(CastTests, SignedToSigned_SameType_int64_t) { cast_test_signed_to_signed_same_type<int64_t>(); }

template <typename T>
void
cast_test_unsigned_to_unsigned_same_type()
{
    T greatest  = std::numeric_limits<T>::max();
    T mid       = greatest / 2;
    T zero      = static_cast<T>(0);
    T one       = static_cast<T>(1);

    EXPECT_EQ(m::cast<T>(greatest), greatest);
    EXPECT_EQ(m::cast<T>(mid), mid);
    EXPECT_EQ(m::cast<T>(zero), zero);
    EXPECT_EQ(m::cast<T>(one), one);
}

TEST(CastTests, UnsignedToUnsigned_SameType_uint8_t) { cast_test_unsigned_to_unsigned_same_type<uint8_t>(); }
TEST(CastTests, UnsignedToUnsigned_SameType_uint16_t) { cast_test_unsigned_to_unsigned_same_type<uint16_t>(); }
TEST(CastTests, UnsignedToUnsigned_SameType_uint32_t) { cast_test_unsigned_to_unsigned_same_type<uint32_t>(); }
TEST(CastTests, UnsignedToUnsigned_SameType_uint64_t) { cast_test_unsigned_to_unsigned_same_type<uint64_t>(); }
