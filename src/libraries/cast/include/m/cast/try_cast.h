// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

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

//
// future work:
// 
// Add try_cast() variant that can take a lambda that is std::invoke()d when
// the cast would not succeed; then rephrase the normal try_cast() in terms
// of the extensible.
// 
// This is essentially to allow for custom exceptions to be thrown on overflow
// situations.
//

#include <exception>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace m
{
    template <typename FromType, typename ToType, typename Enable = void>
    struct try_cast_helper;

    //
    // It would be nice if a single helper could be used for all integral types
    // but getting the math right for signed and unsigned is remarkably
    // difficult. Instead, we will have four specializations for the
    // FromType and ToType being signed and unsigned.
    //

    // Signed -> Signed
    template <typename FromType, typename ToType>
    struct try_cast_helper<
        FromType,
        ToType,
        std::enable_if_t<std::is_integral_v<FromType> && std::is_integral_v<ToType> &&
                         !std::is_same_v<FromType, bool> && !std::is_same_v<ToType, bool> &&
                         std::is_signed_v<FromType> && std::is_signed_v<ToType>>>
    {
        static constexpr decltype(auto)
        do_cast(FromType v)
        {
            if constexpr (std::numeric_limits<ToType>::digits <
                          std::numeric_limits<FromType>::digits)
            {
                if (v < (std::numeric_limits<ToType>::min)())
                    throw std::overflow_error("v");
            }

            if constexpr (std::numeric_limits<ToType>::digits <
                          std::numeric_limits<FromType>::digits)
            {
                if (v > (std::numeric_limits<ToType>::max)())
                    throw std::overflow_error("v");
            }

            return static_cast<ToType>(v);
        }
    };

    // Unsigned -> Signed
    template <typename FromType, typename ToType>
    struct try_cast_helper<
        FromType,
        ToType,
        std::enable_if_t<std::is_integral_v<FromType> && std::is_integral_v<ToType> &&
                         !std::is_same_v<FromType, bool> && !std::is_same_v<ToType, bool> &&
                         !std::is_signed_v<FromType> && std::is_signed_v<ToType>>>
    {
        static constexpr decltype(auto)
        do_cast(FromType v)
        {
            if constexpr (std::numeric_limits<ToType>::digits <
                          std::numeric_limits<FromType>::digits)
            {
                // The representation of ToType is smaller than FromType, so
                // its max value is representable in FromType, which is
                // unsigned.
                if (v > static_cast<FromType>((std::numeric_limits<ToType>::max)()))
                    throw std::overflow_error("v");

                // Otherwise there is no opportunity for overflow
            }

            return static_cast<ToType>(v);
        }
    };

    // Signed -> Unsigned
    template <typename FromType, typename ToType>
    struct try_cast_helper<
        FromType,
        ToType,
        std::enable_if_t<std::is_integral_v<FromType> && std::is_integral_v<ToType> &&
                         !std::is_same_v<FromType, bool> && !std::is_same_v<ToType, bool> &&
                         std::is_signed_v<FromType> && !std::is_signed_v<ToType>>>
    {
        static constexpr decltype(auto)
        do_cast(FromType v)
        {
            if (v < 0)
                throw std::overflow_error("v");

            if constexpr (std::numeric_limits<ToType>::digits <
                          std::numeric_limits<FromType>::digits)
            {
                if (v > (std::numeric_limits<ToType>::max)())
                    throw std::overflow_error("v");
            }

            return static_cast<ToType>(v);
        }
    };

    // Unsigned -> Unsigned
    template <typename FromType, typename ToType>
    struct try_cast_helper<
        FromType,
        ToType,
        std::enable_if_t<std::is_integral_v<FromType> && std::is_integral_v<ToType> &&
                         !std::is_same_v<FromType, bool> && !std::is_same_v<ToType, bool> &&
                         !std::is_signed_v<FromType> && !std::is_signed_v<ToType>>>
    {
        static constexpr decltype(auto)
        do_cast(FromType v)
        {
            if constexpr (std::numeric_limits<ToType>::digits <
                          std::numeric_limits<FromType>::digits)
            {
                if (v > (std::numeric_limits<ToType>::max)())
                    throw std::overflow_error("v");
            }

            return static_cast<ToType>(v);
        }
    };

    // Base type -> derived type
    template <typename FromType, typename ToType>
    struct try_cast_helper<FromType *, ToType *, std::enable_if_t<std::is_base_of_v<FromType, ToType>>>
    {
        static constexpr ToType*
        do_cast(FromType* v)
        {
            auto p = dynamic_cast<ToType*>(v);
            if (p == nullptr)
                throw std::runtime_error("Unable to downcast pointer safely");
            return p;
        }
    };

    template <typename ToType, typename FromType>
    constexpr decltype(auto)
    try_cast(FromType const& from)
    {
        return try_cast_helper<FromType, ToType>::do_cast(from);
    }

} // namespace m
