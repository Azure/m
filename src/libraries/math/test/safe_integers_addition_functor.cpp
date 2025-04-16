// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <m/cast/to.h>
#include <m/math/math.h>
#include <m/math/integer_functor_macros.h>

namespace T
{
    enum class U8 : uint8_t;
    enum class I8 : int8_t;
    enum class U16 : uint16_t;
    enum class I16 : int16_t;
    enum class U32 : uint32_t;
    enum class I32 : int32_t;
    enum class U64 : uint64_t;
    enum class I64 : int64_t;
    enum class UPTR : uintptr_t;
    enum class UMAX : uintmax_t;
    enum class IMAX : intmax_t;
    enum class PDIFF : ptrdiff_t;
} // namespace T

constexpr decltype(auto)
operator+(T::U8 l, T::U8 r)
{
    return m::addition_functor(l, r);
}

TEST(SafeIntFunctorAdd, U8pU8toU8)
{
    auto v1 = T::U8{10};
    auto v2 = T::U8{20};
    auto v3 = T::U8{255};

    auto sum = (v1 + v2).to<T::U8>();
    EXPECT_EQ(std::to_underlying(sum), 30);

    auto sum1 = m::to<T::U8>(v1 + v2);
    EXPECT_EQ(std::to_underlying(sum1), 30);

    // The sum does not throw because we haven't constrained the addition
    // to some particular result space yet
    auto sum2 = v2 + v3;

    // The sum fits in a 16 bit integer so there is no exception
    EXPECT_EQ(std::to_underlying(sum2.to<T::U16>()), 275);

    EXPECT_THROW(sum2.to<T::U8>(), std::overflow_error);
}
