// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utf/transcode.h>
#include <m/utility/concepts.h>

namespace m
{
    template <typename FromT, typename ToCharT>
        requires(m::character<ToCharT>)
    struct string_conversion_helper
    {
        string_conversion_helper()                                = delete;
        string_conversion_helper(string_conversion_helper const&) = delete;
        string_conversion_helper&
        operator=(string_conversion_helper const&) = delete;
    };

    template <typename ToCharT, typename FromT>
        requires(m::character<ToCharT>)
    std::basic_string_view<ToCharT>
    to_string_view_t(FromT const& from) noexcept
    {
        // using CleanFromT = std::remove_const_t<std::remove_reference_t<FromT>>;
        return string_conversion_helper<FromT, ToCharT>::xlate_to_view(from);
    }

    template <typename ToCharT, typename FromT>
        requires(m::character<ToCharT>)
    std::basic_string<ToCharT>
    to_string_t(FromT&& from) noexcept
    {
        using CleanFromT = std::remove_const_t<std::remove_reference_t<FromT>>;
        return string_conversion_helper<CleanFromT, ToCharT>::xlate_to_string(
            std::forward<FromT>(from));
    }

#ifdef WIN32
    template <>
    struct string_conversion_helper<std::u16string_view, wchar_t>
    {
        static_assert(sizeof(wchar_t) == sizeof(char16_t));

        using from_char_type = char16_t;
        using to_char_type   = wchar_t;

        using from_view_type = std::basic_string_view<from_char_type>;
        using to_view_type   = std::basic_string_view<to_char_type>;

        using from_string_type = std::basic_string<from_char_type>;
        using to_string_type   = std::basic_string<to_char_type>;

        static to_view_type
        xlate_to_view(from_view_type view) noexcept
        {
            return to_view_type(reinterpret_cast<to_char_type const*>(view.data()), view.size());
        }

        static to_string_type
        xlate_to_string(from_view_type view) noexcept
        {
            return to_string_type(xlate_to_view(view));
        }
    };

    template <>
    struct string_conversion_helper<std::wstring_view, char16_t>
    {
        static_assert(sizeof(wchar_t) == sizeof(char16_t));

        using from_char_type = wchar_t;
        using to_char_type   = char16_t;

        using from_view_type = std::basic_string_view<from_char_type>;
        using to_view_type   = std::basic_string_view<to_char_type>;

        using from_string_type = std::basic_string<from_char_type>;
        using to_string_type   = std::basic_string<to_char_type>;

        static to_view_type
        xlate_to_view(from_view_type view) noexcept
        {
            return to_view_type(reinterpret_cast<to_char_type const*>(view.data()), view.size());
        }

        static to_string_type
        xlate_to_string(from_view_type view) noexcept
        {
            return to_string_type(xlate_to_view(view));
        }
    };

#endif

    template <>
    struct string_conversion_helper<std::string_view, char>
    {
        using from_char_type = char;
        using to_char_type   = char;

        using from_view_type = std::basic_string_view<from_char_type>;
        using to_view_type   = std::basic_string_view<to_char_type>;

        using from_string_type = std::basic_string<from_char_type>;
        using to_string_type   = std::basic_string<to_char_type>;

        static to_view_type
        xlate_to_view(from_view_type view) noexcept
        {
            return view;
        }

        static to_string_type
        xlate_to_string(from_view_type view) noexcept
        {
            return to_string_type(view);
        }
    };

    template <>
    struct string_conversion_helper<char const*, char> :
        string_conversion_helper<std::string_view, char>
    {
        using base_t = string_conversion_helper<std::string_view, char>;
        using base_t::from_view_type;

        static to_view_type
        xlate_to_view(char const* str) noexcept
        {
            return base_t::xlate_to_view(from_view_type(str));
        }

        static to_string_type
        xlate_to_string(char const* str) noexcept
        {
            return base_t::to_string_type(from_view_type(str));
        }
    };

    template <std::size_t N>
    struct string_conversion_helper<char[N], char> :
        string_conversion_helper<std::string_view, char>
    {
        using base_t = string_conversion_helper<std::string_view, char>;
        using base_t::from_view_type;

        static to_view_type
        xlate_to_view(char const* str) noexcept
        {
            return base_t::xlate_to_view(from_view_type(str));
        }

        static to_string_type
        xlate_to_string(char const* str) noexcept
        {
            return base_t::to_string_type(from_view_type(str));
        }
    };

    template <>
    struct string_conversion_helper<std::u8string_view, char8_t>
    {
        using from_char_type = char8_t;
        using to_char_type   = char8_t;

        using from_view_type = std::basic_string_view<from_char_type>;
        using to_view_type   = std::basic_string_view<to_char_type>;

        using from_string_type = std::basic_string<from_char_type>;
        using to_string_type   = std::basic_string<to_char_type>;

        static to_view_type
        xlate_to_view(from_view_type view) noexcept
        {
            return view;
        }

        static to_string_type
        xlate_to_string(from_view_type view) noexcept
        {
            return to_string_type(view);
        }
    };

    template <>
    struct string_conversion_helper<std::u16string_view, char16_t>
    {
        using from_char_type = char16_t;
        using to_char_type   = char16_t;

        using from_view_type = std::basic_string_view<from_char_type>;
        using to_view_type   = std::basic_string_view<to_char_type>;

        using from_string_type = std::basic_string<from_char_type>;
        using to_string_type   = std::basic_string<to_char_type>;

        static to_view_type
        xlate_to_view(from_view_type view) noexcept
        {
            return view;
        }

        static to_string_type
        xlate_to_string(from_view_type view) noexcept
        {
            return to_string_type(view);
        }
    };

    template <>
    struct string_conversion_helper<std::u32string_view, char32_t>
    {
        using from_char_type = char32_t;
        using to_char_type   = char32_t;

        using from_view_type = std::basic_string_view<from_char_type>;
        using to_view_type   = std::basic_string_view<to_char_type>;

        using from_string_type = std::basic_string<from_char_type>;
        using to_string_type   = std::basic_string<to_char_type>;

        static to_view_type
        xlate_to_view(from_view_type view) noexcept
        {
            return view;
        }

        static to_string_type
        xlate_to_string(from_view_type view) noexcept
        {
            return to_string_type(view);
        }
    };

} // namespace m
