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
    struct string_converter;

// Partial specialization of functions is non-functional, so we use
// a struct with static functions as an intermediate.
//
// It makes the task somewhat cumbersome but effective.
//
#if 0
    template <typename FromT, typename ToType, typename Enable = void>
    struct string_converter
    {
        //
        // string_converter may contain two named static member functions:
        //
        // make_string() which translated strings between representations.
        // make_string() will in general allocate memory and can fail,
        // even when the translation can be constexpr (e.g. UTF-8 to
        // UTF-16).
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
