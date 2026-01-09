// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <concepts>
#include <optional>
#include <type_traits>

#include <m/utility/concepts.h>
#include <m/utility/type_traits.h>

namespace m
{
    template <typename T>
        requires(character<T>)
    class basic_sstring;

    template <typename T, typename TChar>
    concept stringish =
        std::same_as<remove_cvref_t<T>, std::basic_string<TChar>> ||
        std::same_as<remove_cvref_t<T>, std::basic_string_view<TChar>> ||
        std::same_as<remove_cvref_t<T>, m::basic_sstring<TChar>> ||
        std::same_as<remove_cvref_t<T>, TChar*> || std::same_as<remove_cvref_t<T>, TChar const*>;

    template <typename T>
    concept any_stringish =
        std::same_as<remove_cvref_t<T>, std::basic_string<char>> ||
        std::same_as<remove_cvref_t<T>, std::basic_string<wchar_t>> ||
        std::same_as<remove_cvref_t<T>, std::basic_string<char8_t>> ||
        std::same_as<remove_cvref_t<T>, std::basic_string<char16_t>> ||
        std::same_as<remove_cvref_t<T>, std::basic_string<char32_t>> ||
        std::same_as<remove_cvref_t<T>, std::basic_string_view<char>> ||
        std::same_as<remove_cvref_t<T>, std::basic_string_view<wchar_t>> ||
        std::same_as<remove_cvref_t<T>, std::basic_string_view<char8_t>> ||
        std::same_as<remove_cvref_t<T>, std::basic_string_view<char16_t>> ||
        std::same_as<remove_cvref_t<T>, std::basic_string_view<char32_t>> ||
        std::same_as<remove_cvref_t<T>, m::basic_sstring<char>> ||
        std::same_as<remove_cvref_t<T>, m::basic_sstring<wchar_t>> ||
        std::same_as<remove_cvref_t<T>, m::basic_sstring<char8_t>> ||
        std::same_as<remove_cvref_t<T>, m::basic_sstring<char16_t>> ||
        std::same_as<remove_cvref_t<T>, m::basic_sstring<char32_t>> ||
        std::same_as<remove_cvref_t<T>, char*> || std::same_as<remove_cvref_t<T>, wchar_t*> ||
        std::same_as<remove_cvref_t<T>, char8_t*> || std::same_as<remove_cvref_t<T>, char16_t*> ||
        std::same_as<remove_cvref_t<T>, char32_t*> ||
        std::same_as<remove_cvref_t<T>, char const*> ||
        std::same_as<remove_cvref_t<T>, wchar_t const*> ||
        std::same_as<remove_cvref_t<T>, char8_t const*> ||
        std::same_as<remove_cvref_t<T>, char16_t const*> ||
        std::same_as<remove_cvref_t<T>, char32_t const*>;

    template <typename T, typename Enable = void>
    struct stringish_char_type;

    template <typename T>
    struct stringish_char_type<
        T,
        std::enable_if_t<std::same_as<remove_cvref_t<T>, std::basic_string<char>> ||
                         std::same_as<remove_cvref_t<T>, std::basic_string_view<char>> ||
                         std::same_as<remove_cvref_t<T>, m::basic_sstring<char>> ||
                         std::same_as<remove_cvref_t<T>, char*> ||
                         std::same_as<remove_cvref_t<T>, char const*>>>
    {
        using type = char;
    };

    template <typename T>
    struct stringish_char_type<
        T,
        std::enable_if_t<std::same_as<remove_cvref_t<T>, std::basic_string<wchar_t>> ||
                         std::same_as<remove_cvref_t<T>, std::basic_string_view<wchar_t>> ||
                         std::same_as<remove_cvref_t<T>, m::basic_sstring<wchar_t>> ||
                         std::same_as<remove_cvref_t<T>, wchar_t*> ||
                         std::same_as<remove_cvref_t<T>, wchar_t const*>>>
    {
        using type = wchar_t;
    };

    template <typename T>
    struct stringish_char_type<
        T,
        std::enable_if_t<std::same_as<remove_cvref_t<T>, std::basic_string<char8_t>> ||
                         std::same_as<remove_cvref_t<T>, std::basic_string_view<char8_t>> ||
                         std::same_as<remove_cvref_t<T>, m::basic_sstring<char8_t>> ||
                         std::same_as<remove_cvref_t<T>, char8_t*> ||
                         std::same_as<remove_cvref_t<T>, char8_t const*>>>
    {
        using type = char8_t;
    };

    template <typename T>
    struct stringish_char_type<
        T,
        std::enable_if_t<std::same_as<remove_cvref_t<T>, std::basic_string<char16_t>> ||
                         std::same_as<remove_cvref_t<T>, std::basic_string_view<char16_t>> ||
                         std::same_as<remove_cvref_t<T>, m::basic_sstring<char16_t>> ||
                         std::same_as<remove_cvref_t<T>, char16_t*> ||
                         std::same_as<remove_cvref_t<T>, char16_t const*>>>
    {
        using type = char16_t;
    };

    template <typename T>
    struct stringish_char_type<
        T,
        std::enable_if_t<std::same_as<remove_cvref_t<T>, std::basic_string<char32_t>> ||
                         std::same_as<remove_cvref_t<T>, std::basic_string_view<char32_t>> ||
                         std::same_as<remove_cvref_t<T>, m::basic_sstring<char32_t>> ||
                         std::same_as<remove_cvref_t<T>, char32_t*> ||
                         std::same_as<remove_cvref_t<T>, char32_t const*>>>
    {
        using type = char32_t;
    };

    template <typename T>
    using stringish_char_type_t = typename stringish_char_type<T>::type;

} // namespace m
