// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <concepts>
#include <type_traits>

#include <m/utility/to_underlying.h>

//
// Macros to define operations on classes of `enum`s for C++
//
// Scoped enums are great except that common things like | and &
// don't work with them when you want to use them as flags or
// the like.
//
// These macros will define the usual operations that you might
// want for these scoped enumerations. Unfortunately they can't
// define some things like operator bool.
//
// A different idiom is required for the relatively common:
//
// if (x & ~(f1 | f2 | f3))
//
// to test for whether x has any bits set outside of f1, f2 and f3.
// Maybe C++26 will provide an idiomatic constexpr solution that
// isn't terrible to behold.
//
// Note that, fairly obviously, the bit-wise operations almost
// certainly return values which are not defined in the
// enumeration, unless you have things like:
//
// enum class MyFlags {
//    DoX = 1,
//    DoY = 2,
//    DoXAndY = 3,
// };
//
// But nobody does that, do they?
//
//

#define M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(T)                                                        \
    static_assert(std::is_enum_v<T>);                                                              \
    constexpr bool operator!(T x) noexcept { return m::to_underlying(x) == 0; }                    \
    constexpr T    operator~(T x) noexcept { return static_cast<T>(~m::to_underlying(x)); }        \
    constexpr T    operator|(T l, T r) noexcept                                                    \
    {                                                                                              \
        return static_cast<T>(m::to_underlying(l) | m::to_underlying(r));                          \
    }                                                                                              \
    constexpr T operator&(T l, T r) noexcept                                                       \
    {                                                                                              \
        return static_cast<T>(m::to_underlying(l) & m::to_underlying(r));                          \
    }                                                                                              \
    constexpr T operator^(T l, T r) noexcept                                                       \
    {                                                                                              \
        return static_cast<T>(m::to_underlying(l) ^ m::to_underlying(r));                          \
    }                                                                                              \
    constexpr T& operator|=(T& l, T r) noexcept                                                    \
    {                                                                                              \
        l = static_cast<T>(m::to_underlying(l) | m::to_underlying(r));                             \
        return l;                                                                                  \
    }                                                                                              \
    constexpr T& operator&=(T& l, T r) noexcept                                                    \
    {                                                                                              \
        l = static_cast<T>(m::to_underlying(l) & m::to_underlying(r));                             \
        return l;                                                                                  \
    }

namespace m
{
    template <typename T>
        requires(std::is_scoped_enum_v<T>)
    bool
    excess_bits_set(T value, T valid_bits)
    {
        return !!(value & ~valid_bits);
    }
} // namespace m
