// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <compare>
#include <type_traits>

#include <m/math/math.h>

#include <m/type_traits/type_traits.h>

#include "functors.h"

namespace m
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
}; // namespace m

#pragma push_macro("M_DEFINE_UNARY_FUNCTORS")
#pragma push_macro("M_DEFINE_BINARY_FUNCTORS")
#pragma push_macro("M_DEFINE_FUNCTORS_")
#pragma push_macro("M_DEFINE_FUNCTORS")

#undef M_DEFINE_UNARY_FUNCTORS
#undef M_DEFINE_BINARY_FUNCTORS
#undef M_DEFINE_FUNCTORS_
#undef M_DEFINE_FUNCTORS

#define M_DEFINE_UNARY_FUNCTORS(T)                                                                 \
    constexpr decltype(auto) operator-(T v) { return m::negation_functor(v); }

#define M_DEFINE_ADDITIVE_FUNCTORS(TLEFT, TRIGHT)                                                  \
    constexpr decltype(auto) operator+(TLEFT l, TRIGHT r) { return m::addition_functor(l, r); };   \
    constexpr decltype(auto) operator-(TLEFT l, TRIGHT r) { return m::subtraction_functor(l, r); };

#define M_DEFINE_MULTIPLICATIVE_FUNCTORS(TLEFT, TRIGHT)                                            \
    constexpr decltype(auto) operator*(TLEFT l, TRIGHT r)                                          \
    {                                                                                              \
        return m::multiplication_functor(l, r);                                                    \
    };                                                                                             \
    constexpr decltype(auto) operator/(TLEFT l, TRIGHT r) { return m::division_functor(l, r); };

#define M_DEFINE_BINARY_FUNCTORS(TLEFT, TRIGHT)                                                    \
    M_DEFINE_ADDITIVE_FUNCTORS(TLEFT, TRIGHT)                                                      \
    M_DEFINE_MULTIPLICATIVE_FUNCTORS(TLEFT, TRIGHT)

#define M_DEFINE_FUNCTORS_(TLEFT, TRIGHT)                                                          \
    M_DEFINE_UNARY_FUNCTORS(TLEFT)                                                                 \
    M_DEFINE_BINARY_FUNCTORS(TLEFT, TRIGHT)

#define M_DEFINE_FUNCTORS(T) M_DEFINE_FUNCTORS_(T, T)

M_DEFINE_FUNCTORS(m::U8);
M_DEFINE_FUNCTORS(m::I8);
M_DEFINE_FUNCTORS(m::U16);
M_DEFINE_FUNCTORS(m::I16);
M_DEFINE_FUNCTORS(m::U32);
M_DEFINE_FUNCTORS(m::I32);
M_DEFINE_FUNCTORS(m::U64);
M_DEFINE_FUNCTORS(m::I64);
M_DEFINE_FUNCTORS(m::UMAX);
M_DEFINE_FUNCTORS(m::IMAX);
M_DEFINE_FUNCTORS(m::PDIFF);
M_DEFINE_FUNCTORS(m::UPTR);

#pragma pop_macro("M_DEFINE_UNARY_FUNCTORS")
#pragma pop_macro("M_DEFINE_BINARY_FUNCTORS")
#pragma pop_macro("M_DEFINE_FUNCTORS_")
#pragma pop_macro("M_DEFINE_FUNCTORS")
