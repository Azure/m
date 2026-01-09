// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <concepts>
#include <optional>
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
    static inline constexpr bool is_integral_non_bool_v = is_integral_non_bool<T>::value;

    template <typename T>
    static inline constexpr bool is_unsigned_integral_non_bool_v =
        (is_integral_non_bool<T>::value && std::is_unsigned_v<T>);

    template <typename T>
    static inline constexpr bool is_signed_integral_non_bool_v =
        (is_integral_non_bool<T>::value && std::is_signed_v<T>);

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
    inline constexpr bool are_integral_non_bool_types_v =
        are_integral_non_bool_types<T1, T2>::value;

    template <typename T, typename... Types>
    inline constexpr bool is_any_of_v = // true if and only if T is in Types
        (std::is_same_v<T, Types> || ...);

    template <typename T, typename EnableT = void>
    inline constexpr bool is_transparent_v = false;

    template <typename T>
    inline constexpr bool is_transparent_v<T, std::void_t<typename T::is_transparent>> = true;

    template <typename T>
    struct is_transparent : std::bool_constant<is_transparent_v<T>>
    {};

    template <typename CandidateType, template <typename...> typename Template>
    inline constexpr bool is_specialization_v =
        false; // true if and only if CandidateType is a specialization of Template

    template <template <typename...> class Template, typename... Types>
    inline constexpr bool is_specialization_v<Template<Types...>, Template> = true;

    template <typename CandidateType, template <typename...> class Template>
    struct is_specialization : std::bool_constant<is_specialization_v<CandidateType, Template>>
    {};

    template <typename T>
    using remove_cvref_t = std::remove_const_t<std::remove_volatile_t<std::remove_reference_t<T>>>;

    template <typename T>
    struct remove_optional
    {
        using type = T;
    };

    template <typename T>
    struct remove_optional<std::optional<T>>
    {
        using type = T;
    };

    template <typename T>
    using remove_optional_t = remove_optional<T>::type;
} // namespace m
