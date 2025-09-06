// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <m/strings/tstring.h>
#include <m/utf/transcode.h>

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
    to_wstring(m::czstring szstr, std::wstring& str);

    std::wstring
    to_wstring(m::czstring szstr);

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
    to_u8string(m::czstring szstr, std::u8string& str);

    std::u8string
    to_u8string(m::czstring szstr);

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
    to_u16string(m::czstring szstr, std::u16string& str);

    std::u16string
    to_u16string(m::czstring szstr);

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
    to_u32string(m::czstring szstr, std::u32string& str);

    std::u32string
    to_u32string(m::czstring szstr);

    void
    to_u32string(std::string_view v, std::u32string& str);

    std::u32string
    to_u32string(std::string_view v);


    template <>
    struct string_conversion_helper<char, char16_t>
    {
        using from_char_type = char16_t;
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

} // namespace m
