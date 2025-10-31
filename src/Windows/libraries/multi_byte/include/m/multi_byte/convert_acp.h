// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <m/strings/string_conversion_details.h>
#include <m/strings/tstring.h>
#include <m/utf/transcode.h>
#include <m/utility/pointers.h>

#include "convert.h"

//
// This header defines functions in the m namespace which will provide
// fluent conversion between the strongly typed UTF-8/UTF-16/UTF-32 and
// both char on the assumption that char is encoded using CP_ACP and
// wchar_t on the assumption that wchar_t is encoded as UTF-16.
//
// These are, more or less, the tacit assumptions on Windows.
//

namespace m
{
    namespace string_conversion_details
    {
        template <>
        std::basic_string<char>
        string_view_to_string<wchar_t, char>(std::basic_string_view<wchar_t> const&);

        template <>
        std::basic_string<char>
        string_view_to_string<char8_t, char>(std::basic_string_view<char8_t> const&);

        template <>
        std::basic_string<char>
        string_view_to_string<char16_t, char>(std::basic_string_view<char16_t> const&);

        template <>
        std::basic_string<char>
        string_view_to_string<char32_t, char>(std::basic_string_view<char32_t> const&);

        template <>
        std::basic_string<wchar_t>
        string_view_to_string<char, wchar_t>(std::basic_string_view<char> const&);

        template <>
        std::basic_string<wchar_t>
        string_view_to_string<char8_t, wchar_t>(std::basic_string_view<char8_t> const&);

        template <>
        std::basic_string<wchar_t>
        string_view_to_string<char16_t, wchar_t>(std::basic_string_view<char16_t> const&);

        template <>
        std::basic_string<wchar_t>
        string_view_to_string<char32_t, wchar_t>(std::basic_string_view<char32_t> const&);


        template <>
        std::basic_string<char8_t>
        string_to_string<char, char8_t>(std::basic_string<char> const&);

        template <>
        std::basic_string<char8_t>
        string_to_string<wchar_t, char8_t>(std::basic_string<wchar_t> const&);

        template <>
        std::basic_string<char16_t>
        string_to_string<char, char16_t>(std::basic_string<char> const&);

        template <>
        std::basic_string<char16_t>
        string_to_string<wchar_t, char16_t>(std::basic_string<wchar_t> const&);

        template <>
        std::basic_string<char32_t>
        string_to_string<char, char32_t>(std::basic_string<char> const&);

        template <>
        std::basic_string<char32_t>
        string_to_string<wchar_t, char32_t>(std::basic_string<wchar_t> const&);

        template <>
        std::basic_string<char>
        string_to_string<wchar_t, char>(std::basic_string<wchar_t> const&);

        template <>
        std::basic_string<char>
        string_to_string<char8_t, char>(std::basic_string<char8_t> const&);

        template <>
        std::basic_string<char>
        string_to_string<char16_t, char>(std::basic_string<char16_t> const&);

        template <>
        std::basic_string<char>
        string_to_string<char32_t, char>(std::basic_string<char32_t> const&);

        template <>
        std::basic_string<wchar_t>
        string_to_string<char, wchar_t>(std::basic_string<char> const&);

        template <>
        std::basic_string<wchar_t>
        string_to_string<char8_t, wchar_t>(std::basic_string<char8_t> const&);

        template <>
        std::basic_string<wchar_t>
        string_to_string<char16_t, wchar_t>(std::basic_string<char16_t> const&);

        template <>
        std::basic_string<wchar_t>
        string_to_string<char32_t, wchar_t>(std::basic_string<char32_t> const&);

        template <>
        std::basic_string<char8_t>
        string_to_string<char, char8_t>(std::basic_string<char> const&);

        template <>
        std::basic_string<char8_t>
        string_to_string<wchar_t, char8_t>(std::basic_string<wchar_t> const&);

        template <>
        std::basic_string<char16_t>
        string_to_string<char, char16_t>(std::basic_string<char> const&);

        template <>
        std::basic_string<char16_t>
        string_to_string<wchar_t, char16_t>(std::basic_string<wchar_t> const&);

        template <>
        std::basic_string<char32_t>
        string_to_string<char, char32_t>(std::basic_string<char> const&);

        template <>
        std::basic_string<char32_t>
        string_to_string<wchar_t, char32_t>(std::basic_string<wchar_t> const&);




    } // namespace string_conversion_details

#if 0
    void
    to_string(std::u8string_view v, std::string& str);

    std::string
    to_string(std::u8string_view v);

    void
    to_string(std::u8string const& s, std::string& str);

    std::string
    to_string(std::u8string const& s);

    void
    to_string(std::optional<std::u8string_view> v, std::optional<std::string>& str);

    std::optional<std::string>
    to_string(std::optional<std::u8string_view> v);

    std::optional<std::string>
    to_string(std::optional<std::u8string> const& s);

    void
    to_string(std::u16string_view v, std::string& str);

    std::string
    to_string(std::u16string_view v);

    void
    to_string(std::u16string const& s, std::string& str);

    std::string
    to_string(std::u16string const& s);

    void
    to_string(char16_t const* s, std::string& str);

    std::string
    to_string(char16_t const* s);

    void
    to_string(std::optional<std::u16string_view> v, std::string& str);

    std::optional<std::string>
    to_string(std::optional<std::u16string_view> v);

    std::optional<std::string>
    to_string(std::optional<std::u16string> const& str);

    void
    to_string(std::u32string_view v, std::string& str);

    std::string
    to_string(std::u32string_view v);

    void
    to_string(std::u32string const& s, std::string& str);

    std::string
    to_string(std::u32string const& s);

    void
    to_string(std::optional<std::u32string_view> v, std::optional<std::string>& str);

    std::optional<std::string>
    to_string(std::optional<std::u32string_view> v);

    std::optional<std::string>
    to_string(std::optional<std::u32string> const& s);

    void
    to_string(std::wstring_view v, std::string& str);

    std::string
    to_string(std::wstring_view v);

    void
    to_string(std::wstring const& s, std::string& str);

    std::string
    to_string(std::wstring const& s);

    void
    to_string(std::optional<std::wstring_view> v, std::optional<std::string>& str);

    std::optional<std::string>
    to_string(std::optional<std::wstring_view> v);

    std::optional<std::string>
    to_string(std::optional<std::wstring> const& s);

    //
    // m::to_wstring
    //

    //
    // char -> wchar_t
    //

    void
    to_wstring(m::czstring szstr, std::optional<std::wstring>& str);

    void
    to_wstring(m::not_null<m::czstring> szstr, std::wstring& str);

    std::optional<std::wstring>
    to_wstring(m::czstring szstr);

    std::wstring
    to_wstring(m::not_null<m::czstring> szstr);

    void
    to_wstring(std::string_view v, std::wstring& str);

    std::wstring
    to_wstring(std::string_view v);

    void
    to_wstring(std::optional<std::string_view> v, std::optional<std::wstring>& str);

    std::optional<std::wstring>
    to_wstring(std::optional<std::string_view> v);

    void
    to_wstring(std::string const& s, std::wstring& str);

    std::wstring
    to_wstring(std::string const& s);

    //
    // m::to_u8string
    //

    //
    // char -> char8_t
    //

    void
    to_u8string(m::czstring szstr, std::optional<std::u8string>& str);

    void
    to_u8string(m::not_null<m::czstring> szstr, std::u8string& str);

    std::optional<std::u8string>
    to_u8string(m::czstring szstr);

    std::u8string
    to_u8string(m::not_null<m::czstring> szstr);

    void
    to_u8string(std::string_view v, std::u8string& str);

    std::u8string
    to_u8string(std::string_view v);

    //
    // m::to_u16string
    //

    //
    // char -> char16_t
    //

    void
    to_u16string(m::czstring szstr, std::optional<std::u16string>& str);

    void
    to_u16string(m::not_null<m::czstring> szstr, std::u16string& str);

    std::optional<std::u16string>
    to_u16string(m::czstring szstr);

    std::u16string
    to_u16string(m::not_null<m::czstring> szstr);

    void
    to_u16string(std::string_view v, std::u16string& str);

    std::u16string
    to_u16string(std::string_view v);

    //
    // m::to_u32string
    //

    //
    // char -> char32_t
    //
    void
    to_u32string(m::czstring szstr, std::optional<std::u32string>& str);

    void
    to_u32string(m::not_null<m::czstring> szstr, std::u32string& str);

    std::optional<std::u32string>
    to_u32string(m::czstring szstr);

    std::u32string
    to_u32string(m::not_null<m::czstring> szstr);

    void
    to_u32string(std::string_view v, std::u32string& str);

    std::u32string
    to_u32string(std::string_view v);

    //
    // This is where all the potentially lossy character conversions
    // live.
    //

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
        xlate_to_string(from_view_type view)
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
        xlate_to_string(from_view_type view)
        {
            return m::to_wstring(view);
        }
    };

    //
    // Since the others don't have uniform names for the
    // conversion functions (which is kind of the point of
    // the m::to_string_t<> function, for which this is
    // the implementation machinery), they require individual
    // attention. If there's a better pattern here, please
    // apply it.
    //
    // In general, for the UTF encodings, on Windows, it's
    // from char and from wchar_t to each of the possibilities.
    // We can use partial specialization to at least avoid
    // writing half of the templates.
    //

    template <typename CharT>
        requires(m::character<CharT> &&
                 (std::is_same_v<CharT, char> || std::is_same_v<CharT, wchar_t>))
    struct string_conversion_helper<std::basic_string_view<CharT>, char8_t>
    {
        using from_char_type = CharT;
        using to_char_type   = char8_t;

        using from_view_type = std::basic_string_view<from_char_type>;
        using to_view_type   = std::basic_string_view<to_char_type>;

        using from_string_type = std::basic_string<from_char_type>;
        using to_string_type   = std::basic_string<to_char_type>;

        static to_string_type
        xlate_to_string(from_view_type view)
        {
            return m::to_u8string(view);
        }
    };

    template <typename CharT>
        requires(m::character<CharT> &&
                 (std::is_same_v<CharT, char> || std::is_same_v<CharT, wchar_t>))
    struct string_conversion_helper<std::basic_string_view<CharT>, char16_t>
    {
        using from_char_type = CharT;
        using to_char_type   = char16_t;

        using from_view_type = std::basic_string_view<from_char_type>;
        using to_view_type   = std::basic_string_view<to_char_type>;

        using from_string_type = std::basic_string<from_char_type>;
        using to_string_type   = std::basic_string<to_char_type>;

        static to_string_type
        xlate_to_string(from_view_type view)
        {
            return m::to_u16string(view);
        }
    };

    template <typename CharT>
        requires(m::character<CharT> &&
                 (std::is_same_v<CharT, char> || std::is_same_v<CharT, wchar_t>))
    struct string_conversion_helper<std::basic_string_view<CharT>, char32_t>
    {
        using from_char_type = CharT;
        using to_char_type   = char32_t;

        using from_view_type = std::basic_string_view<from_char_type>;
        using to_view_type   = std::basic_string_view<to_char_type>;

        using from_string_type = std::basic_string<from_char_type>;
        using to_string_type   = std::basic_string<to_char_type>;

        static to_string_type
        xlate_to_string(from_view_type view)
        {
            return m::to_u32string(view);
        }
    };
#endif
} // namespace m
