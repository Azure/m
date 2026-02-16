// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

//
// try_cast<T>() - Compatibility wrapper
//
// NOTE: This is a legacy API. New code should use m::to<T>() instead.
//
// m::try_cast<T>() is maintained for backward compatibility but is simply
// a thin wrapper around the preferred m::to<T>() API. The name "try_cast"
// is wordy compared to the more idiomatic and concise "to".
//
// These casts are safe in that they will either not compile if values are
// not preserved, or will throw runtime errors if values are not preserved.
//
// Consider using m::to<T>() for ALL casting. static_cast is, in practice,
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

#include <m/cast/to.h>

namespace m
{
    //
    // Compatibility alias: try_cast<T>() forwards to to<T>()
    //
    // Prefer using m::to<T>() directly in new code for its concise, idiomatic syntax.
    //
    template <typename ToType, typename FromType>
    constexpr decltype(auto)
    try_cast(FromType const& from)
    {
        return m::to<ToType>(from);
    }

    //
    // Re-export to_helper as try_cast_helper for backward compatibility
    //
    template <typename FromType, typename ToType, typename Enable = void>
    using try_cast_helper = to_helper<FromType, ToType, Enable>;

} // namespace m
