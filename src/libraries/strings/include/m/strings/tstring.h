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
    template <typename ToCharT, typename FromCharT>
        requires(m::character<ToCharT> && m::character<FromCharT>)
    struct string_conversion_helper;

    template <typename ToCharT, typename FromCharT>
        requires(m::character<ToCharT> && m::character<FromCharT>)
    std::basic_string_view<ToCharT>
    to_string_view_t(std::basic_string_view<FromCharT> view)
    {
        return string_conversion_helper<ToCharT, FromCharT>::xlate_to_view(view);
    }

    template <typename ToCharT, typename FromCharT>
        requires(m::character<ToCharT> && m::character<FromCharT>)
    std::basic_string<ToCharT>
    to_string_t(std::basic_string_view<FromCharT> view)
    {
        return string_conversion_helper<ToCharT, FromCharT>::xlate_to_string(view);
    }

#ifdef WIN32
    template <>
    struct string_conversion_helper<wchar_t, char16_t>
    {
        static_assert(sizeof(wchar_t) == sizeof(char16_t));

        using from_char_type = char16_t;
        using to_char_type   = wchar_t;

        using from_view_type = std::basic_string_view<from_char_type>;
        using to_view_type   = std::basic_string_view<to_char_type>;

        using from_string_type = std::basic_string<from_char_type>;
        using to_string_type   = std::basic_string<to_char_type>;

        static to_view_type
        xlate_to_view(from_view_type view)
        {
            return to_view_type(reinterpret_cast<to_char_type const*>(view.data()), view.size());
        }

        static to_string_type
        xlate_to_string(from_view_type view)
        {
            return to_string_type(xlate_to_view(view));
        }
    };

    template <>
    struct string_conversion_helper<char16_t, wchar_t>
    {
        static_assert(sizeof(wchar_t) == sizeof(char16_t));

        using from_char_type   = wchar_t;
        using to_char_type = char16_t;

        using from_view_type = std::basic_string_view<from_char_type>;
        using to_view_type   = std::basic_string_view<to_char_type>;

        using from_string_type = std::basic_string<from_char_type>;
        using to_string_type   = std::basic_string<to_char_type>;

        static to_view_type
        xlate_to_view(from_view_type view)
        {
            return to_view_type(reinterpret_cast<to_char_type const*>(view.data()), view.size());
        }

        static to_string_type
        xlate_to_string(from_view_type view)
        {
            return to_string_type(xlate_to_view(view));
        }
    };

#endif

    template <>
    struct string_conversion_helper<char, char>
    {
        using from_char_type = char;
        using to_char_type   = char;

        using from_view_type = std::basic_string_view<from_char_type>;
        using to_view_type   = std::basic_string_view<to_char_type>;

        using from_string_type = std::basic_string<from_char_type>;
        using to_string_type   = std::basic_string<to_char_type>;

        static to_view_type
        xlate_to_view(from_view_type view)
        {
            return view;
        }

        static to_string_type
        xlate_to_string(from_view_type view)
        {
            return to_string_type(view);
        }
    };

    template <>
    struct string_conversion_helper<char8_t, char8_t>
    {
        using from_char_type = char8_t;
        using to_char_type   = char8_t;

        using from_view_type = std::basic_string_view<from_char_type>;
        using to_view_type   = std::basic_string_view<to_char_type>;

        using from_string_type = std::basic_string<from_char_type>;
        using to_string_type   = std::basic_string<to_char_type>;

        static to_view_type
        xlate_to_view(from_view_type view)
        {
            return view;
        }

        static to_string_type
        xlate_to_string(from_view_type view)
        {
            return to_string_type(view);
        }
    };

    template <>
    struct string_conversion_helper<char16_t, char16_t>
    {
        using from_char_type = char16_t;
        using to_char_type   = char16_t;

        using from_view_type = std::basic_string_view<from_char_type>;
        using to_view_type   = std::basic_string_view<to_char_type>;

        using from_string_type = std::basic_string<from_char_type>;
        using to_string_type   = std::basic_string<to_char_type>;

        static to_view_type
        xlate_to_view(from_view_type view)
        {
            return view;
        }

        static to_string_type
        xlate_to_string(from_view_type view)
        {
            return to_string_type(view);
        }
    };

    template <>
    struct string_conversion_helper<char32_t, char32_t>
    {
        using from_char_type = char32_t;
        using to_char_type   = char32_t;

        using from_view_type = std::basic_string_view<from_char_type>;
        using to_view_type   = std::basic_string_view<to_char_type>;

        using from_string_type = std::basic_string<from_char_type>;
        using to_string_type   = std::basic_string<to_char_type>;

        static to_view_type
        xlate_to_view(from_view_type view)
        {
            return view;
        }

        static to_string_type
        xlate_to_string(from_view_type view)
        {
            return to_string_type(view);
        }
    };

} // namespace m
