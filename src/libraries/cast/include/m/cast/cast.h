// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <type_traits>

#include <gsl/gsl>

#include <m/type_traits/type_traits.h>

//
// Various safe casts
//
// These casts are safe in that they will either not compile if values are
// not preserved, or will throw runtime errors if values are not preserved.
//
// Consider using these for ALL casting. static_cast is, in practice,
// quite dangerous. It's better than C-style casts but that's not saying
// much. When casting from, say, a 64-bit unsigned integer type like size_t
// to an unsigned 32 bit integer, it trims off the top 32 bits. While we
// rarely deal with such large data in the current era, there will come a
// time where 4gb data streams may become popular, at which time these casts
// will /regularly/ turn into buffer overruns where a static_cast from
// size_t to DWORD turned what should have been a (possibly failed!)
// allocation of 4gb+10 bytes into a 10 byte allocation that succeeds followed
// by a massive buffer overrun.
//
// The framework here works by having a set of intermediate helper types
// that we depend on the compiler to compile away into nothingness by
// virtue of almost all of the work done with them being done by constexpr
// members.
//

namespace m
{
    template <typename FromType, typename ToType, typename Enable = void>
    struct cast_helper;

    //
    // Casting from one integral to another where the min and max of the
    // from type are statically determinable to be within the bounds of
    // the to type.
    //
    // By converting to the signed equivalent types for the comparison of
    // the min() values, the default integral promotions will not cause
    // any strange value conversions to happen as would happen when
    // trying to convert the most negative integer to its unsigned
    // equivalent's most negative (0).
    //
    // The symmetric use of the technique on the max() side is probably
    // unnecessary but provided for the symmetry. There appears to be no
    // negative consequences and it seems like a good idea.
    //
    template <typename FromType, typename ToType>
    struct cast_helper<FromType,
                       ToType,
                       std::enable_if_t<m::are_integral_non_bool_types_v<FromType, ToType> &&
                                        (std::is_signed_v<FromType> == std::is_signed_v<ToType>) &&
                                        (std::numeric_limits<ToType>::digits >=
                                         std::numeric_limits<FromType>::digits)>>
    {
        constexpr static inline bool is_castable = true;

        // Should be a safe static_cast<>
        constexpr static ToType
        do_cast(FromType v)
        {
            return v;
        }
    };

    template <typename ToType, typename FromType, typename Enable = void>
    struct is_castable
    {
        static inline constexpr bool value = false;
    };

    template <typename ToType, typename FromType>
    struct is_castable<ToType, FromType, std::enable_if_t<cast_helper<FromType, ToType>::is_castable>>
    {
        static inline constexpr bool value = true;
    };

    template <typename ToType, typename FromType>
    constexpr bool is_castable_v = is_castable<ToType, FromType>::value;

    template <typename ToType, typename FromType>
    constexpr decltype(auto)
    cast(FromType const& from)
    {
        return cast_helper<FromType, ToType>::do_cast(from);
    }
} // namespace m
