// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#ifdef __GNUG__

#error The Gnu G++ compiler version 14.2 does not support user defined string literals and so cannot use this header

#endif

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <version>

namespace m
{
    template <typename CharT, typename CharTraitsT = std::char_traits<CharT>>
    class basic_literal_string_view;

    namespace string_view_literals
    {
        constexpr basic_literal_string_view<char>
        operator""_sl(char const* str, std::size_t len);

        constexpr basic_literal_string_view<wchar_t>
        operator""_sl(wchar_t const* str, std::size_t len);

#ifdef __cpp_char8_t
        constexpr basic_literal_string_view<char8_t>
        operator""_sl(char8_t const* str, std::size_t len);
#endif

        constexpr basic_literal_string_view<char16_t>
        operator""_sl(char16_t const* str, std::size_t len);

        constexpr basic_literal_string_view<char32_t>
        operator""_sl(char32_t const* str, std::size_t len);
    } // namespace string_view_literals

    template <typename CharT, typename CharTraitsT>
    class basic_literal_string_view : public std::basic_string_view<CharT, CharTraitsT>
    {
        using base_t        = std::basic_string_view<CharT, CharTraitsT>;
        using char_t        = CharT;
        using char_traits_t = CharTraitsT;
        using this_t        = basic_literal_string_view<CharT, CharTraitsT>;

    public:
    protected:
        constexpr basic_literal_string_view(const CharT* str) noexcept:
            base_t(str, char_traits_t::length(str))
        {}

        constexpr basic_literal_string_view(const CharT* str, std::size_t len) noexcept:
            base_t(str, len)
        {}

        friend constexpr basic_literal_string_view<char>
        string_view_literals::operator""_sl(const char*, std::size_t);

        friend constexpr basic_literal_string_view<wchar_t>
        string_view_literals::operator""_sl(const wchar_t*, std::size_t);

#ifdef __cpp_char8_t
        friend constexpr basic_literal_string_view<char8_t>
        string_view_literals::operator""_sl(const char8_t*, std::size_t);
#endif

        friend constexpr basic_literal_string_view<char16_t>
        string_view_literals::operator""_sl(const char16_t*, std::size_t);

        friend constexpr basic_literal_string_view<char32_t>
        string_view_literals::operator""_sl(const char32_t*, std::size_t);
    };

    namespace string_view_literals
    {
        inline constexpr basic_literal_string_view<char>
        operator""_sl(const char* str, std::size_t len)
        {
            return basic_literal_string_view<char>(str, len);
        }

        inline constexpr basic_literal_string_view<wchar_t>
        operator""_sl(const wchar_t* str, std::size_t len)
        {
            return basic_literal_string_view<wchar_t>(str, len);
        }

#ifdef __cpp_char8_t
        inline constexpr basic_literal_string_view<char8_t>
        operator""_sl(const char8_t* str, std::size_t len)
        {
            return basic_literal_string_view<char8_t>(str, len);
        }
#endif

        inline constexpr basic_literal_string_view<char16_t>
        operator""_sl(const char16_t* str, std::size_t len)
        {
            return basic_literal_string_view<char16_t>(str, len);
        }

        inline constexpr basic_literal_string_view<char32_t>
        operator""_sl(const char32_t* str, std::size_t len)
        {
            return basic_literal_string_view<char32_t>(str, len);
        }
    } // namespace string_view_literals

    using literal_string_view = basic_literal_string_view<char, std::char_traits<char>>;
    using wliteral_string_view = basic_literal_string_view<wchar_t, std::char_traits<wchar_t>>;
#ifdef __cpp_char8_t
    using u8literal_string_view = basic_literal_string_view<char8_t>;
#endif
    using u16literal_string_view = basic_literal_string_view<char16_t>;
    using u32literal_string_view = basic_literal_string_view<char32_t>;
} // namespace m
