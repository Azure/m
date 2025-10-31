// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <m/utf/transcode.h>
#include <m/utility/pointers.h>

#include "convert.h"

//
// This header defines functions in the m namespace which will provide
// fluent conversion between the strongly typed UTF-8/UTF-16/UTF-32 and
// wchar_t on the assumption that wchar_t is encoded as UTF-16.
//

namespace m
{
    constexpr std::wstring
    to_wstring(std::u8string_view v)
    {
        std::wstring str;
        utf::transcode(v, str);
        return str;
    }

    constexpr void
    to_wstring(std::u8string_view v, std::wstring& str)
    {
        utf::transcode(v, str);
    }

    constexpr std::optional<std::wstring>
    to_wstring(std::optional<std::u8string_view> v)
    {
        if (!v)
            return std::nullopt;

        std::wstring str;
        utf::transcode(v.value(), str);
        return str;
    }

    constexpr void
    to_wstring(std::optional<std::u8string_view> v, std::optional<std::wstring>& str)
    {
        if (v)
        {
            std::wstring t;
            utf::transcode(v.value(), t);
            str = t;
        }
        else
        {
            str = std::nullopt;
        }
    }

    constexpr std::wstring
    to_wstring(std::u8string const& s)
    {
        std::wstring str;
        utf::transcode(std::u8string_view{s}, str);
        return str;
    }

    constexpr void
    to_wstring(std::u8string const& s, std::wstring& str)
    {
        utf::transcode(std::u8string_view{s}, str);
    }

    constexpr std::wstring
    to_wstring(m::not_null<cu8zstring> ptr)
    {
        return to_wstring(std::u8string_view(ptr));
    }

    constexpr std::optional<std::wstring>
    to_wstring(cu8zstring ptr)
    {
        if (ptr)
            return to_wstring(m::not_null<cu8zstring>(ptr));

        return std::nullopt;
    }

    constexpr std::wstring
    to_wstring(std::u16string_view v)
    {
        std::wstring str;
        utf::transcode(v, str);
        return str;
    }

    constexpr void
    to_wstring(std::u16string_view v, std::wstring& str)
    {
        utf::transcode(v, str);
    }

    constexpr std::optional<std::wstring>
    to_wstring(std::optional<std::u16string_view> v)
    {
        if (!v)
            return std::nullopt;

        std::wstring str;
        utf::transcode(v.value(), str);
        return str;
    }

    constexpr void
    to_wstring(std::optional<std::u16string_view> v, std::optional<std::wstring>& str)
    {
        if (v)
        {
            std::wstring t;
            utf::transcode(v.value(), t);
            str = t;
        }
        else
            str = std::nullopt;
    }

    constexpr std::wstring
    to_wstring(std::u16string const& s)
    {
        std::wstring str;
        utf::transcode(std::u16string_view{s}, str);
        return str;
    }

    constexpr void
    to_wstring(std::u16string const& s, std::wstring& str)
    {
        utf::transcode(std::u16string_view{s}, str);
    }

    constexpr std::wstring
    to_wstring(m::not_null<cu16zstring> ptr)
    {
        return to_wstring(std::u16string_view(ptr));
    }

    constexpr std::optional<std::wstring>
    to_wstring(cu16zstring ptr)
    {
        if (ptr == nullptr)
            return std::nullopt;

        return to_wstring(m::not_null<cu16zstring>(ptr));
    }

    constexpr std::wstring
    to_wstring(std::u32string_view v)
    {
        std::wstring str;
        utf::transcode(v, str);
        return str;
    }

    constexpr void
    to_wstring(std::u32string_view v, std::wstring& str)
    {
        utf::transcode(v, str);
    }

    constexpr std::optional<std::wstring>
    to_wstring(std::optional<std::u32string_view> v)
    {
        if (v)
        {
            std::wstring str;
            utf::transcode(v.value(), str);
            return str;
        }

        return std::nullopt;
    }

    constexpr void
    to_wstring(std::optional<std::u32string_view> v, std::optional<std::wstring>& str)
    {
        if (v)
        {
            std::wstring t;
            utf::transcode(v.value(), t);
            str = t;
        }
        else
            str = std::nullopt;
    }

    constexpr std::wstring
    to_wstring(std::u32string const& s)
    {
        std::wstring str;
        utf::transcode(std::u32string_view{s}, str);
        return str;
    }

    constexpr void
    to_wstring(std::u32string const& s, std::wstring& str)
    {
        utf::transcode(std::u32string_view{s}, str);
    }

    constexpr std::wstring
    to_wstring(m::not_null<cu32zstring> ptr)
    {
        return to_wstring(std::u32string_view(ptr));
    }

    constexpr std::optional<std::wstring>
    to_wstring(cu32zstring ptr)
    {
        if (ptr == nullptr)
            return std::nullopt;

        return to_wstring(m::not_null<cu32zstring>(ptr));
    }

    constexpr void
    to_u8string(std::wstring_view v, std::u8string& str)
    {
        utf::transcode(v, str);
    }

    constexpr std::u8string
    to_u8string(std::wstring_view v)
    {
        std::u8string str;
        to_u8string(v, str);
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

    constexpr void
    to_u16string(std::wstring_view v, std::u16string& str)
    {
        utf::transcode(v, str);
    }

    constexpr std::u16string
    to_u16string(std::wstring_view v)
    {
        std::u16string str;
        utf::transcode(v, str);
        return str;
    }

    constexpr std::u16string
    to_u16string(m::not_null<wchar_t const*> ptr)
    {
        return to_u16string(std::wstring_view(ptr));
    }

    constexpr std::optional<std::u16string>
    to_u16string(wchar_t const* ptr)
    {
        if (ptr)
            return to_u16string(m::not_null<wchar_t const*>(ptr));

        return std::nullopt;
    }

    //
    // to_u32string
    //
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
    to_u32string(m::not_null<wchar_t const*> ptr)
    {
        return to_u32string(std::wstring_view(ptr));
    }

    constexpr std::optional<std::u32string>
    to_u32string(wchar_t const* ptr)
    {
        if (ptr)
            return to_u32string(m::not_null<wchar_t const*>(ptr));

        return std::nullopt;
    }

} // namespace m
