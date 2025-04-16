// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <type_traits>

#include <m/math/functors.h>

#include "cast.h"
#include "try_cast.h"

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

namespace m
{
    template <typename TTo, typename TFrom>
        requires std::is_integral_v<TFrom>
    TTo
    to(TFrom v)
    {
        return m::try_cast<TTo>(v);
    }

    template <typename TTo, typename TFrom>
        requires m::math::is_functor<TFrom>
    TTo
    to(TFrom const& v)
    {
        // Invokes the to<TTo> operation on the functor
        return v.template to<TTo>();
    }

    template <typename TTo, typename TFrom>
        requires std::is_enum_v<TFrom>
    TTo
    to(TFrom const& v)
    {
        return m::try_cast<TTo>(std::to_underlying(v));
    }

} // namespace m
