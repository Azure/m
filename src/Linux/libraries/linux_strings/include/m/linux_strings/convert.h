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
#include <m/utility/zstring.h>

namespace m
{
    namespace string_conversion_details
    {
        //
        template <>
        std::basic_string<char>
        string_view_to_string(std::basic_string_view<wchar_t> const&);
        template <>
        std::basic_string<char>
        string_view_to_string(std::basic_string_view<char8_t> const&);
        template <>
        std::basic_string<char>
        string_view_to_string(std::basic_string_view<char16_t> const&);
        template <>
        std::basic_string<char>
        string_view_to_string(std::basic_string_view<char32_t> const&);

        template <>
        std::basic_string<wchar_t>
        string_view_to_string(std::basic_string_view<char> const&);

        template <>
        std::basic_string<char8_t>
        string_view_to_string(std::basic_string_view<char> const&);
        template <>
        std::basic_string<char8_t>
        string_view_to_string(std::basic_string_view<wchar_t> const&);

        template <>
        std::basic_string<char16_t>
        string_view_to_string(std::basic_string_view<char> const&);
        template <>
        std::basic_string<char16_t>
        string_view_to_string(std::basic_string_view<wchar_t> const&);

        template <>
        std::basic_string<char32_t>
        string_view_to_string(std::basic_string_view<char> const&);
        template <>
        std::basic_string<char32_t>
        string_view_to_string(std::basic_string_view<wchar_t> const&);

        template <>
        std::basic_string<char>
        string_view_to_string(std::basic_string_view<wchar_t> const&);
        template <>
        std::basic_string<char>
        string_view_to_string(std::basic_string_view<char8_t> const&);
        template <>
        std::basic_string<char>
        string_view_to_string(std::basic_string_view<char16_t> const&);
        template <>
        std::basic_string<char>
        string_view_to_string(std::basic_string_view<char32_t> const&);

        template <>
        std::basic_string<wchar_t>
        string_to_string(std::basic_string<char> const&);
        template <>
        std::basic_string<wchar_t>
        string_to_string(std::basic_string<char8_t> const&);
        template <>
        std::basic_string<wchar_t>
        string_to_string(std::basic_string<char16_t> const&);
        template <>
        std::basic_string<wchar_t>
        string_to_string(std::basic_string<char32_t> const&);

        template <>
        std::basic_string<char8_t>
        string_to_string(std::basic_string<char> const&);
        template <>
        std::basic_string<char8_t>
        string_to_string(std::basic_string<wchar_t> const&);

        template <>
        std::basic_string<char16_t>
        string_to_string(std::basic_string<char> const&);
        template <>
        std::basic_string<char16_t>
        string_to_string(std::basic_string<wchar_t> const&);

        template <>
        std::basic_string<char32_t>
        string_to_string(std::basic_string<char> const&);
        template <>
        std::basic_string<char32_t>
        string_to_string(std::basic_string<wchar_t> const&);

    } // namespace string_conversion_details

#if 0
    template <typename CharT>
        requires(m::character<CharT>)
    std::optional<std::string>
    to_string(m::basic_zstring<CharT const> ptr)
    {
        return string_conversion_helper<decltype(ptr), char>::xlate_to_string(ptr);
    }

    template <typename CharT>
        requires(m::character<CharT>)
    std::string
    to_string(m::not_null<m::basic_zstring<CharT const>> ptr)
    {
        return string_conversion_helper<decltype(ptr), char>::xlate_to_string(ptr);
    }

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

    constexpr std::wstring
    to_wstring(m::not_null<czstring> ptr)
    {
        return to_wstring(std::string_view(ptr));
    }

    constexpr std::optional<std::wstring>
    to_wstring(czstring ptr)
    {
        if (!ptr)
            return std::nullopt;

        return to_wstring(m::not_null<czstring>(ptr));
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

    constexpr std::wstring
    to_wstring(m::not_null<cu8zstring> ptr)
    {
        return to_wstring(std::u8string_view(ptr));
    }

    constexpr std::optional<std::wstring>
    to_wstring(cu8zstring ptr)
    {
        if (!ptr)
            return std::nullopt;

        return to_wstring(m::not_null<cu8zstring>(ptr));
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

    constexpr std::wstring
    to_wstring(m::not_null<cu32zstring> ptr)
    {
        return to_wstring(std::u32string_view(ptr));
    }

    constexpr std::optional<std::wstring>
    to_wstring(cu32zstring ptr)
    {
        if (!ptr)
            return std::nullopt;

        return to_wstring(m::not_null<cu32zstring>(ptr));
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

    constexpr std::u8string
    to_u8string(m::not_null<czstring> ptr)
    {
        return to_u8string(std::string_view(ptr));
    }

    constexpr std::optional<std::u8string>
    to_u8string(czstring ptr)
    {
        if (!ptr)
            return std::nullopt;

        return to_u8string(m::not_null<czstring>(ptr));
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

    constexpr std::u8string
    to_u8string(m::not_null<cwzstring> ptr)
    {
        return to_u8string(std::wstring_view(ptr));
    }

    constexpr std::optional<std::u8string>
    to_u8string(cwzstring ptr)
    {
        if (!ptr)
            return std::nullopt;

        return to_u8string(m::not_null<cwzstring>(ptr));
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

    constexpr std::u16string
    to_u16string(m::not_null<czstring> ptr)
    {
        return to_u16string(std::string_view(ptr));
    }

    constexpr std::optional<std::u16string>
    to_u16string(czstring ptr)
    {
        if (!ptr)
            return std::nullopt;

        return to_u16string(m::not_null<czstring>(ptr));
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

    constexpr std::u16string
    to_u16string(m::not_null<cwzstring> ptr)
    {
        return to_u16string(std::wstring_view(ptr));
    }

    constexpr std::optional<std::u16string>
    to_u16string(cwzstring ptr)
    {
        if (!ptr)
            return std::nullopt;

        return to_u16string(m::not_null<cwzstring>(ptr));
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

    constexpr std::u32string
    to_u32string(m::not_null<czstring> ptr)
    {
        return to_u32string(std::string_view(ptr));
    }

    constexpr std::optional<std::u32string>
    to_u32string(czstring ptr)
    {
        if (!ptr)
            return std::nullopt;

        return to_u32string(m::not_null<czstring>(ptr));
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

    constexpr std::u32string
    to_u32string(m::not_null<cwzstring> ptr)
    {
        return to_u32string(std::wstring_view(ptr));
    }

    constexpr std::optional<std::u32string>
    to_u32string(cwzstring ptr)
    {
        if (!ptr)
            return std::nullopt;

        return to_u32string(m::not_null<cwzstring>(ptr));
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

        using from_zstring_type = m::basic_zstring<from_char_type>;

        static to_string_type
        xlate_to_string(from_view_type const& view)
        {
            return m::to_string(view);
        }

        static std::optional<to_string_type>
        xlate_to_string(from_zstring_type ptr)
        {
            if (ptr == nullptr)
                return std::nullopt;

            return xlate_to_string(m::not_null(ptr));
        }

        static std::optional<to_string_type>
        xlate_to_string(m::not_null<from_zstring_type> ptr)
        {
            return xlate_to_string(from_view_type(ptr));
        }
    };

    template <typename CharT>
        requires(m::character<CharT> && !std::is_same_v<CharT, char>)
    struct string_conversion_helper<m::basic_zstring<CharT const>, char>
    {
        static std::optional<std::basic_string<char>>
        xlate_to_string(m::basic_zstring<CharT const> ptr)
        {
            if (ptr == nullptr)
                return std::nullopt;

            auto nnptr = m::not_null<decltype(ptr)>(ptr);
            return string_conversion_helper<decltype(nnptr), char>(nnptr);
        }
    };

    template <typename CharT>
        requires(m::character<CharT> && !std::is_same_v<CharT, char>)
    struct string_conversion_helper<m::not_null<m::basic_zstring<CharT const>>, char>
    {
        static std::basic_string<char>
        xlate_to_string(m::not_null<m::basic_zstring<CharT const>> ptr)
        {
            return string_conversion_helper<std::basic_string_view<CharT>, char>::xlate_to_string(
                std::basic_string_view<char>(ptr));
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
#endif
} // namespace m
