// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <m/utility/string_converter.h>

#include <m/utility/concepts.h>
#include <m/utility/pointers.h>
#include <m/utility/zstring.h>

namespace m::string_conversion_details
{
    template <typename T>
    using decay_remove_cvref_t = remove_cvref_t<std::decay_t<T>>;

    template <typename T>
    struct is_not_null
    {
        static inline constexpr bool value = false;
    };

    template <typename T>
    struct is_not_null<not_null<T>>
    {
        static inline constexpr bool value = true;
    };

    template <typename T>
    constexpr bool is_not_null_v = is_not_null<T>::value;

    template <typename T>
    struct plain_type
    {
        using type = decay_remove_cvref_t<T>;
    };

    template <typename T>
    struct not_null_type
    {
        using type = typename T::value_type;
    };

    template <typename T>
    struct conversion_strip
    {
        using type = typename plain_type<T>::type;
    };

    template <typename T>
    struct conversion_strip<not_null<T>>
    {
        using type = T;
    };

    template <typename T>
    struct conversion_strip<std::optional<T> const&>
    {
        using type = conversion_strip<T>::type;
    };

    template <typename T>
    struct conversion_strip<std::optional<T>&>
    {
        using type = conversion_strip<T>::type;
    };

    template <typename T>
    struct conversion_strip<T*>
    {
        using type = T*;
    };

    template <typename T>
    using conversion_strip_t = conversion_strip<T>::type;

    template <typename T>
    struct string_conversion_equivalent_string
    {
        using type = std::basic_string<typename plain_type<T>::type>;
    };

    template <typename T>
    struct string_conversion_equivalent_string<std::basic_string<T>>
    {
        using type = std::basic_string<T>;
    };

    template <typename T>
    struct string_conversion_equivalent_string<std::basic_string_view<T>>
    {
        using type = std::basic_string<T>;
    };

    template <typename T>
    struct string_conversion_equivalent_string<m::basic_sstring<T>>
    {
        using type = std::basic_string<T>;
    };

    template <typename T>
    using string_conversion_equivalent_string_t = string_conversion_equivalent_string<T>::type;

    template <typename CharT, typename FromT>
    struct basic_string_with_equivalent_optionality
    {
        using type = std::basic_string<CharT>;
    };

    template <typename CharT, typename FromT>
    struct basic_string_with_equivalent_optionality<CharT, std::optional<FromT>>
    {
        using type = std::optional<std::basic_string<CharT>>;
    };

    template <typename CharT, typename FromT>
    using basic_string_with_equivalent_optionality_t =
        basic_string_with_equivalent_optionality<CharT, FromT>::type;

    template <typename CharT, typename FromT>
    struct basic_sstring_with_equivalent_optionality
    {
        using type = m::basic_sstring<CharT>;
    };

    template <typename CharT, typename FromT>
    struct basic_sstring_with_equivalent_optionality<CharT, std::optional<FromT>>
    {
        using type = std::optional<m::basic_sstring<CharT>>;
    };

    template <typename CharT, typename FromT>
    using basic_sstring_with_equivalent_optionality_t =
        basic_sstring_with_equivalent_optionality<CharT, FromT>::type;
} // namespace m::string_conversion_details
