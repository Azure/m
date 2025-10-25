// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <concepts>
#include <type_traits>

namespace m
{
    template <typename T, typename... Types>
    constexpr bool is_any_of_v = // true if and only if T is in Types
        (std::is_same_v<T, Types> || ...);

    template <typename T, typename EnableT = void>
    constexpr bool is_transparent_v = false;

    template <typename T>
    constexpr bool is_transparent_v<T, std::void_t<typename T::is_transparent>> = true;

    template <typename T>
    struct is_transparent : std::bool_constant<is_transparent_v<T>>
    {};

} // namespace m
