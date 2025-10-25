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

#include <concepts>
#include <exception>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include <m/utility/to_underlying.h>

namespace m
{
    template <typename FromType, typename ToType, typename Enable = void>
    struct try_cast_helper
    {
        try_cast_helper()                       = delete;
        try_cast_helper(try_cast_helper const&) = delete;
        try_cast_helper&
        operator=(try_cast_helper const&) = delete;
    };

    template <typename ToType, typename FromType>
    constexpr decltype(auto)
    try_cast(FromType const& from);

    //
    // It would be nice if a single helper could be used for all integral types
    // but getting the math right for signed and unsigned is remarkably
    // difficult. Instead, we will have four specializations for the
    // FromType and ToType being signed and unsigned.
    //

    // Signed -> Signed
    template <typename FromType, typename ToType>
        requires(std::signed_integral<FromType> && std::signed_integral<ToType>)
    struct try_cast_helper<FromType, ToType, void>
    {
        static constexpr decltype(auto)
        do_cast(FromType v)
        {
            if constexpr (std::numeric_limits<ToType>::digits <
                          std::numeric_limits<FromType>::digits)
            {
                if (v < (std::numeric_limits<ToType>::min)())
                {
                    throw std::overflow_error("v");
                }
            }

            if constexpr (std::numeric_limits<ToType>::digits <
                          std::numeric_limits<FromType>::digits)
            {
                if (v > (std::numeric_limits<ToType>::max)())
                {
                    throw std::overflow_error("v");
                }
            }

            return static_cast<ToType>(v);
        }
    };

    // Unsigned -> Signed
    template <typename FromType, typename ToType>
        requires(std::unsigned_integral<FromType> && std::signed_integral<ToType>)
    struct try_cast_helper<FromType, ToType, void>
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
                {
                    throw std::overflow_error("v");
                }

                // Otherwise there is no opportunity for overflow
            }

            return static_cast<ToType>(v);
        }
    };

    // Signed -> Unsigned
    template <typename FromType, typename ToType>
        requires(std::signed_integral<FromType> && std::unsigned_integral<ToType>)
    struct try_cast_helper<FromType, ToType, void>
    {
        static constexpr decltype(auto)
        do_cast(FromType v)
        {
            if (v < 0)
            {
                throw std::overflow_error("v");
            }

            if constexpr (std::numeric_limits<ToType>::digits <
                          std::numeric_limits<FromType>::digits)
            {
                if (v > (std::numeric_limits<ToType>::max)())
                {
                    throw std::overflow_error("v");
                }
            }

            return static_cast<ToType>(v);
        }
    };

    // Unsigned -> Unsigned
    template <typename FromType, typename ToType>
        requires(std::unsigned_integral<FromType> && std::unsigned_integral<ToType>)
    struct try_cast_helper<FromType, ToType, void>
    {
        static constexpr decltype(auto)
        do_cast(FromType v)
        {
            if constexpr (std::numeric_limits<ToType>::digits <
                          std::numeric_limits<FromType>::digits)
            {
                if (v > (std::numeric_limits<ToType>::max)())
                {
                    throw std::overflow_error("v");
                }
            }

            return static_cast<ToType>(v);
        }
    };

    // Enable casting from std::chrono::duration
    template <typename Rep, typename Period, typename ToType>
        requires(std::integral<Rep>)
    struct try_cast_helper<std::chrono::duration<Rep, Period>, ToType, void>;

    // Enable casting from std::chrono::time_point
    template <typename Clock, typename Duration, typename ToType>
    struct try_cast_helper<std::chrono::time_point<Clock, Duration>, ToType, void>;

    // Base type -> derived type
    template <typename FromType, typename ToType>
    struct try_cast_helper<FromType*,
                           ToType*,
                           std::enable_if_t<std::is_base_of_v<FromType, ToType>>>
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
        requires(std::is_enum_v<FromType>)
    struct try_cast_helper<FromType, ToType, void>
    {
        static constexpr ToType
        do_cast(FromType const& v)
        {
            FromType   v1 = v;
            auto const t  = m::to_underlying(v1);
            return m::try_cast<ToType>(t);
        }
    };

    template <typename ToType, typename FromType>
    constexpr decltype(auto)
    try_cast(FromType const& from)
    {
        using cast_helper_t = try_cast_helper<FromType, ToType>;
        return cast_helper_t::do_cast(from);
    }

} // namespace m
