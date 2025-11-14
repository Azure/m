// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <concepts>
#include <type_traits>

#include <m/utility/type_traits.h>

namespace m
{
    // Define a concept for character types
    template <typename T>
    concept character =
        std::same_as<T, char> || std::same_as<T, signed char> || std::same_as<T, unsigned char> ||
        std::same_as<T, wchar_t> || std::same_as<T, char8_t> || std::same_as<T, char16_t> ||
        std::same_as<T, char32_t>;

    template <typename T>
    concept transparent = is_transparent_v<T>;

    template <typename T>
        requires(character<T>)
    class basic_sstring;

    template <typename T>
    concept stringish = std::same_as<remove_cvref_t<T>, std::basic_string<char>> ||
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
                        std::same_as<remove_cvref_t<T>, m::basic_sstring<char32_t>>;

    template <typename T>
    concept has_value_type = requires { typename T::value_type; };

} // namespace m
