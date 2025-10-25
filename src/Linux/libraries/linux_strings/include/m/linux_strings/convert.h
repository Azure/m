// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <numeric>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/strings/tstring.h>
#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utf/transcode.h>
#include <m/utility/make_span.h>
#include <m/utility/pointers.h>

namespace m
{
    constexpr void
    to_string(std::wstring_view view, std::string& str)
    {
        utf::transcode(view, str);
    }

    constexpr std::string
    to_string(std::wstring_view view)
    {
        std::string str;
        to_string(view, str);
        return str;
    }

#if 0
    constexpr void
    to_string(std::string_view view, std::string& str)
    {
        str.clear();
        str.reserve(view.size());
        str.assign(view.data(), view.size());
    }

    constexpr std::string
    to_string(std::string_view view)
    {
        std::string str;
        to_string(view, str);
        return str;
    }
#endif

    constexpr void
    to_string(std::u8string_view view, std::string& str)
    {
        utf::transcode(view, str);
    }

    constexpr std::string
    to_string(std::u8string_view view)
    {
        std::string str;
        to_string(view, str);
        return str;
    }

    constexpr void
    to_string(std::u16string_view view, std::string& str)
    {
        utf::transcode(view, str);
    }

    constexpr std::string
    to_string(std::u16string_view view)
    {
        std::string str;
        to_string(view, str);
        return str;
    }

    constexpr void
    to_string(std::u32string_view view, std::string& str)
    {
        utf::transcode(view, str);
    }

    constexpr std::string
    to_string(std::u32string_view view)
    {
        std::string str;
        to_string(view, str);
        return str;
    }

    //
    // to_wstring
    //

    constexpr void
    to_wstring(std::string_view v, std::wstring& str)
    {
        utf::transcode(v, str);
    }

    constexpr std::wstring
    to_wstring(std::string_view v)
    {
        std::wstring str;
        to_wstring(v, str);
        return str;
    }

    constexpr void
    to_wstring(std::u8string_view view, std::wstring& str)
    {
        utf::transcode(view, str);
    }

    constexpr std::wstring
    to_wstring(std::u8string_view view)
    {
        std::wstring str;
        to_wstring(view, str);
        return str;
    }

    constexpr void
    to_wstring(std::u16string_view view, std::wstring& str)
    {
        utf::transcode(view, str);
    }

    constexpr std::wstring
    to_wstring(std::u16string_view view)
    {
        std::wstring str;
        to_wstring(view, str);
        return str;
    }

    constexpr std::optional<std::wstring>
    to_wstring(char16_t const* ptr)
    {
        if (ptr != nullptr)
            return to_wstring(std::u16string_view(ptr));

        return std::nullopt;
    }

    constexpr std::wstring
    to_wstring(m::not_null<char16_t const*> ptr)
    {
        return to_wstring(std::u16string_view(ptr));
    }

    constexpr void
    to_wstring(std::u32string_view view, std::wstring& str)
    {
        utf::transcode(view, str);
    }

    constexpr std::wstring
    to_wstring(std::u32string_view view)
    {
        std::wstring str;
        to_wstring(view, str);
        return str;
    }

    //
    // to_u8string
    //

    constexpr void
    to_u8string(std::string_view v, std::u8string& str)
    {
        utf::transcode(v, str);
    }

    constexpr std::u8string
    to_u8string(std::string_view v)
    {
        std::u8string str;
        to_u8string(v, str);
        return str;
    }

    constexpr void
    to_u8string(std::wstring_view view, std::u8string& str)
    {
        utf::transcode(view, str);
    }

    constexpr std::u8string
    to_u8string(std::wstring_view view)
    {
        std::u8string str;
        to_u8string(view, str);
        return str;
    }

    //
    // to_u16string
    //
    constexpr void
    to_u16string(std::string_view view, std::u16string& str)
    {
        utf::transcode(view, str);
    }

    constexpr std::u16string
    to_u16string(std::string_view view)
    {
        std::u16string str;
        to_u16string(view, str);
        return str;
    }

    constexpr void
    to_u16string(std::wstring_view view, std::u16string& str)
    {
        utf::transcode(view, str);
    }

    constexpr std::u16string
    to_u16string(std::wstring_view view)
    {
        std::u16string str;
        to_u16string(view, str);
        return str;
    }

    //
    // to_u32string
    //
    constexpr void
    to_u32string(std::string_view view, std::u32string& str)
    {
        utf::transcode(view, str);
    }

    constexpr std::u32string
    to_u32string(std::string_view view)
    {
        std::u32string str;
        to_u32string(view, str);
        return str;
    }

    constexpr void
    to_u32string(std::wstring_view view, std::u32string& str)
    {
        utf::transcode(view, str);
    }

    constexpr std::u32string
    to_u32string(std::wstring_view view)
    {
        std::u32string str;
        to_u32string(view, str);
        return str;
    }

    template <typename CharT>
        requires(m::character<CharT> && !std::is_same_v<CharT, char>)
    struct string_conversion_helper<std::basic_string_view<CharT>, char>
    {
        using from_char_type = CharT;
        using to_char_type   = char;

        using from_view_type = std::basic_string_view<from_char_type>;
        using to_view_type   = std::basic_string_view<to_char_type>;

        using from_string_type = std::basic_string<from_char_type>;
        using to_string_type   = std::basic_string<to_char_type>;

        static to_string_type
        xlate_to_string(from_view_type const& view)
        {
            return m::to_string(view);
        }
    };

    template <typename CharT>
        requires(m::character<CharT> && !std::is_same_v<CharT, wchar_t>)
    struct string_conversion_helper<std::basic_string_view<CharT>, wchar_t>
    {
        using from_char_type = CharT;
        using to_char_type   = wchar_t;

        using from_view_type = std::basic_string_view<from_char_type>;
        using to_view_type   = std::basic_string_view<to_char_type>;

        using from_string_type = std::basic_string<from_char_type>;
        using to_string_type   = std::basic_string<to_char_type>;

        static to_string_type
        xlate_to_string(from_view_type const& view)
        {
            return m::to_wstring(view);
        }
    };
} // namespace m
