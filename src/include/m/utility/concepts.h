// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <concepts>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/utility/platform.h>
#include <m/utility/type_traits.h>

namespace m
{
    // Define a concept for character types
    template <typename T>
    concept character =
        std::same_as<T, char> || std::same_as<T, signed char> || std::same_as<T, unsigned char> ||
        std::same_as<T, wchar_t> || std::same_as<T, char8_t> || std::same_as<T, char16_t> ||
        std::same_as<T, char32_t>;

#if M_WCHAR_T_IS_UTF16

    template <typename T>
    concept utf16_character = std::same_as<T, wchar_t> || std::same_as<T, char16_t>;

#else

    template <typename T>
    concept utf16_character = std::same_as<T, char16_t>;

#endif

    template <typename T, typename CharT>
    concept has_view = requires(T x) {
        { x.view() } noexcept -> std::same_as<std::basic_string_view<CharT>>;
    };

    template <typename T>
    concept has_some_view = (has_view<T, char> || has_view<T, char8_t> || has_view<T, char16_t> ||
                             has_view<T, char32_t> || has_view<T, wchar_t>);
    template <typename T>
    concept has_value_type = requires { typename T::value_type; };

} // namespace m
