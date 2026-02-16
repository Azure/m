// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

//
// Standard metaphor across the m codebase
//
// m::to<T>(expr)
//
// This is a "safe" cast/retrieval that gets the value of "expr", as a T. If
// it cannot retrieve the value of "expr" as a "T" with "100% fidelity", an
// exception is thrown.
//
// There are a lot of indirections there, so we will discuss.
//
// First, the intent.
//
// Coders should be able to "always" put m::to<T>(x) in anywhere, and if it
// compiles, trust that the returned value is safe to use, meaning that the
// value did not lose any precision or is referencing memory in an unsafe
// fashion.
//
// For inputs that are integers, this means that no values are lost. If T
// cannot hold the value in x, a std::overflow_error exception is thrown,
// period.
//
// For inputs that are pointers, if the cast from decltype(x) to T cannot
// be done safely, an exception is thrown. (I don't know the exception type
// offhand but it will derive from std::runtime_error.)
//
// Safe in this context means:
//
// - T is the same type as or a base type of decltype(x)
//
// - a dynamic_cast<T>(x) returned a non-nullptr value
//
// When the input is a floating point type, the definition here is less
// clear. if std::is_same_v<decltype(x), T> then m::to<T>(x) == x. It's
// mostly clear that double -> float, in general is unlikely to succeed,
// it's unclear whether a runtime check whether a particular value will
// round trip is of any worth. Zero would but the set of other values
// that will round trip would seem somewhat arbitrary.
//
// float -> double would seem to make sense, but in practice, it's
// unverified what happens with all the NaN values and what happens with
// all of the various patterns of mantissa and exponent bits. Anecdotal
// evidence is that this will be implementation dependent and code path
// dependent, where obviously in a case where a 32 bit value simply has
// a zero added to it could not trigger a floating point error but if
// floating point instructions are used, a different result could occur.
//
// [MicGrier: I'd prefer to simply avoid floating point or only allow
// T -> T conversions?]
//
// Other, more arbitrary conversions could be allowed but it is very important
// for the implementor to keep in mind the requirement for fidelity and
// round-trip safety. The intent of this construct is to avoid the problems
// inherent in the C++ implicit integer promotion, static_cast<>,
// reinterpret_cast<> etc. casts.
//
//
//
// The other use of m::to<T>(expr) is for things much like std::get<>().
//
// The example here is the safe math operation functors. They take
// the operations on the "safe int" types and wrap, for example, addition
// in an addition functor. The actual addition is not performed until the
// output type is known since whether the operation would overflow is not
// known until then.
//
// The solution is to use m::to<T>(x+y), so that we have a relatively
// uniform syntax for type/numeric conversion.
//
//

#include <chrono>
#include <concepts>
#include <exception>
#include <format>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <typeinfo>

#include <m/utility/to_underlying.h>

namespace m
{
    //
    // Forward declaration
    //
    template <typename ToType, typename FromType>
    constexpr decltype(auto)
    to(FromType const& from);

    //
    // Helper struct for type conversions
    //
    template <typename FromType, typename ToType, typename Enable = void>
    struct to_helper
    {
        to_helper()                   = delete;
        to_helper(to_helper const&)   = delete;
        to_helper& operator=(to_helper const&) = delete;
    };

    //
    // It would be nice if a single helper could be used for all integral types
    // but getting the math right for signed and unsigned is remarkably
    // difficult. Instead, we will have four specializations for the
    // FromType and ToType being signed and unsigned.
    //

    // Signed -> Signed
    template <typename FromType, typename ToType>
        requires(std::signed_integral<FromType> && std::signed_integral<ToType>)
    struct to_helper<FromType, ToType, void>
    {
        static constexpr decltype(auto)
        do_cast(FromType v)
        {
            if constexpr (std::numeric_limits<ToType>::digits <
                          std::numeric_limits<FromType>::digits)
            {
                if (v < (std::numeric_limits<ToType>::min)())
                {
                    throw std::overflow_error(std::format(
                        "m::to overflow: value {} is less than minimum {} for target type",
                        v,
                        (std::numeric_limits<ToType>::min)()));
                }
            }

            if constexpr (std::numeric_limits<ToType>::digits <
                          std::numeric_limits<FromType>::digits)
            {
                if (v > (std::numeric_limits<ToType>::max)())
                {
                    throw std::overflow_error(std::format(
                        "m::to overflow: value {} exceeds maximum {} for target type",
                        v,
                        (std::numeric_limits<ToType>::max)()));
                }
            }

            return static_cast<ToType>(v);
        }
    };

    // Unsigned -> Signed
    template <typename FromType, typename ToType>
        requires(std::unsigned_integral<FromType> && std::signed_integral<ToType>)
    struct to_helper<FromType, ToType, void>
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
                    throw std::overflow_error(std::format(
                        "m::to overflow: value {} exceeds maximum {} for target type",
                        v,
                        (std::numeric_limits<ToType>::max)()));
                }

                // Otherwise there is no opportunity for overflow
            }

            return static_cast<ToType>(v);
        }
    };

    // Signed -> Unsigned
    template <typename FromType, typename ToType>
        requires(std::signed_integral<FromType> && std::unsigned_integral<ToType>)
    struct to_helper<FromType, ToType, void>
    {
        static constexpr decltype(auto)
        do_cast(FromType v)
        {
            if (v < 0)
            {
                throw std::overflow_error(std::format(
                    "m::to overflow: negative value {} cannot be converted to unsigned type",
                    v));
            }

            if constexpr (std::numeric_limits<ToType>::digits <
                          std::numeric_limits<FromType>::digits)
            {
                if (v > (std::numeric_limits<ToType>::max)())
                {
                    throw std::overflow_error(std::format(
                        "m::to overflow: value {} exceeds maximum {} for target type",
                        v,
                        (std::numeric_limits<ToType>::max)()));
                }
            }

            return static_cast<ToType>(v);
        }
    };

    // Unsigned -> Unsigned
    template <typename FromType, typename ToType>
        requires(std::unsigned_integral<FromType> && std::unsigned_integral<ToType>)
    struct to_helper<FromType, ToType, void>
    {
        static constexpr decltype(auto)
        do_cast(FromType v)
        {
            if constexpr (std::numeric_limits<ToType>::digits <
                          std::numeric_limits<FromType>::digits)
            {
                if (v > (std::numeric_limits<ToType>::max)())
                {
                    throw std::overflow_error(std::format(
                        "m::to overflow: value {} exceeds maximum {} for target type",
                        v,
                        (std::numeric_limits<ToType>::max)()));
                }
            }

            return static_cast<ToType>(v);
        }
    };

    // Enable casting from std::chrono::duration
    template <typename Rep, typename Period, typename ToType>
        requires(std::integral<Rep>)
    struct to_helper<std::chrono::duration<Rep, Period>, ToType, void>;

    // Enable casting from std::chrono::time_point
    template <typename Clock, typename Duration, typename ToType>
    struct to_helper<std::chrono::time_point<Clock, Duration>, ToType, void>;

    // Base type -> derived type
    template <typename FromType, typename ToType>
    struct to_helper<FromType*,
                     ToType*,
                     std::enable_if_t<std::is_base_of_v<FromType, ToType>>>
    {
        static constexpr ToType*
        do_cast(FromType* v)
        {
            auto p = dynamic_cast<ToType*>(v);
            if (p == nullptr)
            {
                throw std::runtime_error(std::format(
                    "m::to failed: unable to safely downcast pointer from {} to {}",
                    typeid(FromType).name(),
                    typeid(ToType).name()));
            }
            return p;
        }
    };

    // Enum -> Integral
    template <typename ToType, typename FromType>
        requires(std::is_enum_v<FromType>)
    struct to_helper<FromType, ToType, void>
    {
        static constexpr ToType
        do_cast(FromType const& v)
        {
            auto const t = m::to_underlying(v);
            return m::to<ToType>(t);
        }
    };

    //
    // Primary API: m::to<T>()
    //
    template <typename ToType, typename FromType>
    constexpr decltype(auto)
    to(FromType const& from)
    {
        using helper_t = to_helper<FromType, ToType>;
        return helper_t::do_cast(from);
    }

} // namespace m
