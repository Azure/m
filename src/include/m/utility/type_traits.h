// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <concepts>
#include <optional>
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

    template <typename CandidateType, template <typename...> typename Template>
    constexpr bool is_specialization_v = false; // true if and only if CandidateType is a specialization of Template

    template <template <typename...> class Template, typename... Types>
    constexpr bool is_specialization_v<Template<Types...>, Template> = true;

    template <typename CandidateType, template <typename...> class Template>
    struct is_specialization : std::bool_constant<is_specialization_v<CandidateType, Template>>
    {};

    template <typename T>
    using remove_optional_t =
        std::conditional_t<std::is_same_v<T, std::optional<typename T::value_type>>,
                           typename T::value_type,
                           T>;

    template <typename T>
    using value_type_of_t = remove_optional_t<T>::value_type;

    template <typename T>
    using remove_cvref_t = std::remove_const_t<std::remove_volatile_t<std::remove_reference_t<T>>>;

} // namespace m

