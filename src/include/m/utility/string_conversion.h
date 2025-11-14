// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <concepts>
#include <type_traits>

#include <m/utility/concepts.h>
#include <m/utility/type_traits.h>

namespace m
{
    namespace string_conversion_details
    {
        // Partial specialization of functions is non-functional, so we use
        // a struct with static functions as an intermediate.
        //
        // It makes the task somewhat cumbersome but effective.
        //

        template <typename FromT, typename ToType>
            requires(character<value_type_of_t<ToType>>)
        struct sch // string conversion helper -- super wordy otherwise
        {
            //
            // sch may contain two named static member functions:
            // 
            // make_string() which translated strings between representations.
            // make_string() will in general allocate memory and can fail,
            // even when the translation can be constexpr (e.g. UTF-8 to
            // UTF-16).
            // 
            // make_view() which is only implemented when a lossless
            // basic_string_view<T> to basic_string_view<U> conversion
            // can be done. Obviously this means that the underlying
            // representation of T and U equivalent.
            // 
            // These sets differ in general between Windows and Linux.
            // 
            // On Windows, the equivalence sets are:
            // 
            // { wchar_t, char16_t }
            // 
            // On Linux they are:
            // 
            // { char, char8_t }
            // { wchar_t, char32_t }
            // 
            // Colloquially, on Windows, wchar_t is UTF-16. On Linux, char
            // is UTF-8 and wchar_t is UTF-32.
            // 
            // Since we're defining equivalence classes, it's tempting to
            // include { signed char, unsigned char } in the set with char
            // whenever it appears, but this just explodes the matrices and
            // we're hand coding all of this. If you're actually using
            // signed or unsigned char, well, bon chance.
            //

            //
            // Perhaps in the future a different api for getting lengths,
            // possible constexpr conversion of UTF encodings etc could be
            // provided but at this time this is to round up all the mish-
            // mash of conventions under one more orderly standard.
            //
        };

    } // namespace string_conversion_details
} // namespace m
