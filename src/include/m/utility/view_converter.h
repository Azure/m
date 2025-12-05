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
    template <typename FromT, typename ToT, typename Enable = void>
    struct view_converter;

// Partial specialization of functions is non-functional, so we use
// a struct with static functions as an intermediate.
//
// It makes the task somewhat cumbersome but effective.
//
#if 0 // left for commenting
    template <typename FromT, typename ToType, typename Enable = void>
    struct view_converter
    {
        //
        // view_converter may contain two named static member functions:
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
        // { char, char8_t }e
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
#endif
} // namespace m
