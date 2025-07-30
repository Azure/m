// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <type_traits>

namespace m
{

    template <typename T, typename enabled = void>
    struct is_integral_non_bool
    {
        static constexpr bool value = false;
    };

    template <typename T>
    struct is_integral_non_bool<
        T,
        std::enable_if_t<std::is_integral_v<T> && !::std::is_same_v<T, bool>>>
    {
        static constexpr bool value = true;
    };

    template <typename T>
    constexpr bool is_integral_non_bool_v = is_integral_non_bool<T>::value;


    template <typename T1, typename T2, typename enabled = void>
    struct are_integral_non_bool_types
    {
        static constexpr bool value = false;
    };

    template <typename T1, typename T2>
    struct are_integral_non_bool_types<
        T1,
        T2,
        std::enable_if_t<std::is_integral_v<T1> && std::is_integral_v<T2> &&
                         !::std::is_same_v<T1, bool> && !::std::is_same_v<T2, bool>>>
    {
        static constexpr bool value = true;
    };

    template <typename T1, typename T2>
    constexpr bool are_integral_non_bool_types_v = are_integral_non_bool_types<T1, T2>::value;

}