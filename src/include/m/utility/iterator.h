// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <concepts>
#include <iterator>
#include <type_traits>

#include <m/utility/type_traits.h>

namespace m
{
    //
    // std::back_insert_iterator does not (easily) yield some of its secrets like most
    // iterators, so these operations try to help make up the difference.
    //

    template <typename T>
    constexpr bool is_back_insert_iterator_v = m::is_specialization_v<T, std::back_insert_iterator>;

    template <typename T>
    struct is_back_insert_iterator : std::bool_constant<is_back_insert_iterator_v<T>>
    {};

    template <typename T>
    using iterator_value_type_t = std::conditional_t<is_back_insert_iterator_v<T>,
                                                     typename T::container_type::value_type,
                                                     typename std::iterator_traits<T>::value_type>;

} // namespace m
