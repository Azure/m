// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <m/strings/tstring.h>
#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utf/transcode.h>
#include <m/utility/pointers.h>

namespace m
{
    //
    // to_string
    //

    constexpr void to_string(std::nullptr_t) = delete;

    constexpr std::string
    to_string(char const* str)
    {
        return std::string(str);
    }

    //
    // std::string -> std::string
    // std::string_view -> std::string
    // std::optional<std::string_view> -> std::optional<std::string>
    //

    constexpr std::string
    to_string(std::string_view v)
    {
        return std::string(v);
    }

    constexpr void
    to_string(std::string_view v, std::string& str)
    {
        str = v;
    }

    constexpr std::string
    to_string(std::string const& s)
    {
        return std::string(std::string_view{s});
    }

    constexpr void
    to_string(std::string const& s, std::string& str)
    {
        str = s;
    }

    constexpr std::optional<std::string>
    to_string(std::optional<std::string_view> v)
    {
        if (v)
            return std::string(v.value());

        return std::nullopt;
    }

    constexpr void
    to_string(std::optional<std::string_view> v, std::optional<std::string>& str)
    {
        if (v)
            str = v;
        else
            str = std::nullopt;
    }

    //
    //  m::to_wstring
    //

    constexpr void to_wstring(std::nullptr_t) = delete;

    constexpr std::wstring
    to_wstring(wchar_t const* str)
    {
        return std::wstring{str};
    }

    //
    // std::wstring -> std::wstring
    // std::wstring_view -> std::wstring
    // std::optional<std::wstring> -> std::optional<std::wstring> ** only type that implements this
    // pattern at this moment std::optional<std::wstring_view> -> std::optional<std::wstring>
    //
    constexpr std::wstring
    to_wstring(std::wstring_view v)
    {
        return std::wstring(v);
    }

    constexpr void
    to_wstring(std::wstring_view v, std::wstring& str)
    {
        str = v;
    }

    constexpr std::wstring
    to_wstring(std::wstring const& s)
    {
        return std::wstring(s);
    }

    constexpr void
    to_wstring(std::wstring const& s, std::wstring& str)
    {
        str = s;
    }

    constexpr std::optional<std::wstring>
    to_wstring(std::optional<std::wstring_view> v)
    {
        if (v)
            return std::wstring(v.value());

        return std::nullopt;
    }

    constexpr void
    to_wstring(std::optional<std::wstring_view> v, std::optional<std::wstring>& str)
    {
        if (v)
            str = v;
        else
            str = std::nullopt;
    }

    constexpr std::optional<std::wstring>
    to_wstring(std::optional<std::wstring> const& s)
    {
        if (s)
            return std::wstring(s.value());

        return std::nullopt;
    }

    constexpr void
    to_wstring(std::optional<std::wstring> s, std::optional<std::wstring>& str)
    {
        if (s)
            str = s;
        else
            str = std::nullopt;
    }

    //
    // to_u8string
    //

    constexpr void to_u8string(std::nullptr_t) = delete;

    constexpr std::optional<std::u8string>
    to_u8string(char8_t const* ptr)
    {
        if (ptr == nullptr)
            return std::nullopt;

        return std::u8string(std::u8string_view{ptr});
    }

    constexpr std::u8string
    to_u8string(m::not_null<char8_t const*> ptr)
    {
        return std::u8string(std::u8string_view{ptr});
    }

    constexpr std::optional<std::u8string>
    to_u8string(char16_t const* ptr)
    {
        if (ptr == nullptr)
            return std::nullopt;

        std::u8string str;
        utf::transcode(std::u16string_view{ptr}, str);
        return str;
    }

    constexpr std::u8string
    to_u8string(m::not_null<char16_t const*> ptr)
    {
        std::u8string str;
        utf::transcode(std::u16string_view{ptr}, str);
        return str;
    }

    constexpr std::u8string
    to_u8string(m::not_null<char32_t const*> ptr)
    {
        std::u8string str;
        utf::transcode(std::u32string_view{ptr}, str);
        return str;
    }

    //
    // std::u8string -> std::u8string
    // std::u8string_view -> std::u8string
    // std::optional<std::u8string_view> -> std::optional<std::u8string>
    //

    constexpr std::u8string
    to_u8string(std::u8string_view v)
    {
        return std::u8string(v);
    }

    constexpr void
    to_u8string(std::u8string_view v, std::u8string& str)
    {
        str = v;
    }

    constexpr std::u8string
    to_u8string(std::u8string const& s)
    {
        return std::u8string(std::u8string_view{s});
    }

    constexpr void
    to_u8string(std::u8string const& s, std::u8string& str)
    {
        str = s;
    }

    constexpr std::optional<std::u8string>
    to_u8string(std::optional<std::u8string_view> v)
    {
        if (v)
            return std::u8string(v.value());

        return std::nullopt;
    }

    constexpr void
    to_u8string(std::optional<std::u8string_view> v, std::optional<std::u8string>& str)
    {
        if (v)
            str = v;
        else
            str = std::nullopt;
    }

    //
    //  Transcoding
    //

    //
    // std::u16string -> std::u8string
    // std::u16string_view -> std::u8string
    // std::optional<std::u16string_view> -> std::optional<std::u8string>
    //

    constexpr void
    to_u8string(std::u16string_view v, std::u8string& str)
    {
        utf::transcode(v, str);
    }

    constexpr std::u8string
    to_u8string(std::u16string_view v)
    {
        std::u8string str;
        to_u8string(v, str);
        return str;
    }

    constexpr void
    to_u8string(std::u16string const& s, std::u8string& str)
    {
        utf::transcode(std::u16string_view{s}, str);
    }

    constexpr std::u8string
    to_u8string(std::u16string const& s)
    {
        std::u8string str;
        to_u8string(s, str);
        return str;
    }

    constexpr void
    to_u8string(std::optional<std::u16string_view> v, std::optional<std::u8string>& str)
    {
        if (v)
        {
            std::u8string t;
            utf::transcode(v.value(), t);
            str = t;
        }
        else
            str = std::nullopt;
    }

    constexpr std::optional<std::u8string>
    to_u8string(std::optional<std::u16string_view> v)
    {
        std::optional<std::u8string> str;
        to_u8string(v, str);
        return str;
    }

    //
    // std::u32string -> std::u8string
    // std::u32string_view -> std::u8string
    // std::optional<std::u32string_view> -> std::optional<std::u8string>
    //

    constexpr void
    to_u8string(std::u32string_view v, std::u8string& str)
    {
        utf::transcode(v, str);
    }

    constexpr std::u8string
    to_u8string(std::u32string_view v)
    {
        std::u8string str;
        to_u8string(v, str);
        return str;
    }

    constexpr void
    to_u8string(std::u32string const& s, std::u8string& str)
    {
        utf::transcode(std::u32string_view{s}, str);
    }

    constexpr std::u8string
    to_u8string(std::u32string const& s)
    {
        std::u8string str;
        to_u8string(s, str);
        return str;
    }

    constexpr void
    to_u8string(std::optional<std::u32string_view> v, std::optional<std::u8string>& str)
    {
        if (v)
        {
            std::u8string t;
            utf::transcode(v.value(), t);
            str = t;
        }
        else
            str = std::nullopt;
    }

    constexpr std::optional<std::u8string>
    to_u8string(std::optional<std::u32string_view> v)
    {
        std::optional<std::u8string> str;
        to_u8string(v, str);
        return str;
    }

    //
    // to_u16string
    //

    constexpr void to_u16string(std::nullptr_t) = delete;

    constexpr std::u16string
    to_u16string(char8_t const* ptr)
    {
        std::u16string str;
        utf::transcode(std::u8string_view{ptr}, str);
        return str;
    }

    constexpr std::u16string
    to_u16string(char16_t const* ptr)
    {
        return std::u16string(std::u16string_view{ptr});
    }

    constexpr std::u16string
    to_u16string(char32_t const* ptr)
    {
        std::u16string str;
        utf::transcode(std::u32string_view{ptr}, str);
        return str;
    }

    //
    // std::u8string -> std::u16string
    // std::u8string_view -> std::u16string
    // std::optional<std::u8string_view> -> std::optional<std::u16string>
    //

    constexpr void
    to_u16string(std::u8string_view v, std::u16string& str)
    {
        utf::transcode(v, str);
    }

    constexpr std::u16string
    to_u16string(std::u8string_view v)
    {
        std::u16string str;
        to_u16string(v, str);
        return str;
    }

    constexpr void
    to_u16string(std::u8string const& s, std::u16string& str)
    {
        utf::transcode(std::u8string_view{s}, str);
    }

    constexpr std::u16string
    to_u16string(std::u8string const& s)
    {
        std::u16string str;
        to_u16string(s, str);
        return str;
    }

    constexpr void
    to_u16string(std::optional<std::u8string_view> v, std::optional<std::u16string>& str)
    {
        if (v)
        {
            std::u16string t;
            utf::transcode(v.value(), t);
            str = t;
        }
        else
            str = std::nullopt;
    }

    constexpr std::optional<std::u16string>
    to_u16string(std::optional<std::u8string_view> v)
    {
        if (v)
            return to_u16string(v.value());

        return std::nullopt;
    }

    //
    // std::u16string -> std::u16string
    // std::u16string_view -> std::u16string
    // std::optional<std::u16string_view> -> std::optional<std::u16string>
    //

    constexpr std::u16string
    to_u16string(std::u16string_view v)
    {
        return std::u16string(v);
    }

    constexpr void
    to_u16string(std::u16string_view v, std::u16string& str)
    {
        str = v;
    }

    constexpr std::u16string
    to_u16string(std::u16string const& s)
    {
        return s;
    }

    constexpr void
    to_u16string(std::u16string const& s, std::u16string& str)
    {
        str = s;
    }

    constexpr std::optional<std::u16string>
    to_u16string(std::optional<std::u16string_view> v)
    {
        if (v)
            return std::u16string(v.value());

        return std::nullopt;
    }

    constexpr void
    to_u16string(std::optional<std::u16string_view> v, std::optional<std::u16string>& str)
    {
        str = v;
    }

    //
    // std::u32string -> std::u16string
    // std::u32string_view -> std::u16string
    // std::optional<std::u32string_view> -> std::optional<std::u16string>
    //

    constexpr void
    to_u16string(std::u32string_view v, std::u16string& str)
    {
        utf::transcode(v, str);
    }

    constexpr std::u16string
    to_u16string(std::u32string_view v)
    {
        std::u16string str;
        to_u16string(v, str);
        return str;
    }

    constexpr void
    to_u16string(std::u32string const& s, std::u16string& str)
    {
        utf::transcode(std::u32string_view{s}, str);
    }

    constexpr std::u16string
    to_u16string(std::u32string const& s)
    {
        std::u16string str;
        to_u16string(s, str);
        return str;
    }

    constexpr void
    to_u16string(std::optional<std::u32string_view> v, std::optional<std::u16string>& str)
    {
        if (v)
        {
            std::u16string t;
            utf::transcode(v.value(), t);
            str = t;
        }
        else
            str = std::nullopt;
    }

    constexpr std::optional<std::u16string>
    to_u16string(std::optional<std::u32string_view> v)
    {
        std::optional<std::u16string> str;
        to_u16string(v, str);
        return str;
    }

    //
    // to_u32string
    //

    constexpr void to_u32string(std::nullptr_t) = delete;

    constexpr std::u32string
    to_u32string(char8_t const* ptr)
    {
        std::u32string str;
        utf::transcode(std::u8string_view{ptr}, str);
        return str;
    }
    constexpr std::u32string
    to_u32string(char16_t const* ptr)
    {
        std::u32string str;
        utf::transcode(std::u16string_view{ptr}, str);
        return str;
    }

    constexpr std::u32string
    to_u32string(char32_t const* ptr)
    {
        return std::u32string(std::u32string_view{ptr});
    }

    //
    // std::u8string -> std::u32string
    // std::u8string_view -> std::u32string
    // std::optional<std::u8string_view> -> std::optional<std::u32string>
    //

    constexpr void
    to_u32string(std::u8string_view v, std::u32string& str)
    {
        utf::transcode(v, str);
    }

    constexpr std::u32string
    to_u32string(std::u8string_view v)
    {
        std::u32string str;
        to_u32string(v, str);
        return str;
    }

    constexpr void
    to_u32string(std::u8string const& s, std::u32string& str)
    {
        utf::transcode(std::u8string_view{s}, str);
    }

    constexpr std::u32string
    to_u32string(std::u8string const& s)
    {
        std::u32string str;
        to_u32string(s, str);
        return str;
    }

    constexpr void
    to_u32string(std::optional<std::u8string_view> v, std::optional<std::u32string>& str)
    {
        if (v)
        {
            std::u32string t;
            utf::transcode(v.value(), t);
            str = t;
        }
        else
            str = std::nullopt;
    }

    constexpr std::optional<std::u32string>
    to_u32string(std::optional<std::u8string_view> v)
    {
        std::optional<std::u32string> str;
        to_u32string(v, str);
        return str;
    }

    //
    // std::u16string -> std::u32string
    // std::u16string_view -> std::u32string
    // std::optional<std::u16string_view> -> std::optional<std::u32string>
    //

    constexpr void
    to_u32string(std::u16string_view v, std::u32string& str)
    {
        utf::transcode(v, str);
    }

    constexpr std::u32string
    to_u32string(std::u16string_view v)
    {
        std::u32string str;
        to_u32string(v, str);
        return str;
    }

    constexpr std::u32string
    to_u32string(std::u16string const& s)
    {
        std::u32string str;
        to_u32string(s, str);
        return str;
    }

    constexpr void
    to_u32string(std::u16string const& s, std::u32string& str)
    {
        utf::transcode(std::u16string_view{s}, str);
    }

    constexpr std::optional<std::u32string>
    to_u32string(std::optional<std::u16string_view> v)
    {
        if (v)
            return to_u32string(v.value());

        return std::nullopt;
    }

    constexpr void
    to_u32string(std::optional<std::u16string_view> v, std::optional<std::u32string>& str)
    {
        if (v)
        {
            std::u32string t;
            utf::transcode(v.value(), t);
            str = t;
        }
        else
            str = std::nullopt;
    }

    //
    // std::u32string -> std::u32string
    // std::u32string_view -> std::u32string
    // std::optional<std::u32string_view> -> std::optional<std::u32string>
    //

    constexpr std::u32string
    to_u32string(std::u32string_view v)
    {
        return std::u32string(v);
    }

    constexpr void
    to_u32string(std::u32string_view v, std::u32string& str)
    {
        str = v;
    }

    constexpr std::u32string
    to_u32string(std::u32string const& s)
    {
        return s;
    }

    constexpr void
    to_u32string(std::u32string const& s, std::u32string& str)
    {
        str = s;
    }

    constexpr std::optional<std::u32string>
    to_u32string(std::optional<std::u32string_view> v)
    {
        if (v)
            return std::u32string(v.value());

        return std::nullopt;
    }

    constexpr void
    to_u32string(std::optional<std::u32string_view> v, std::optional<std::u32string>& str)
    {
        str = v;
    }

} // namespace m
