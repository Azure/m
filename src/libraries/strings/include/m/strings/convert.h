// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <string>
#include <string_view>

#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utf/transcode.h>

namespace m
{
    //
    // to_string
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

    //
    // to_wstring, across all the safe input types
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

    //
    // to_u8string
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

    //
    // to_u16string
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

    //
    // to_u32string
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
    to_u32string(std::u32string_view v)
    {
        return std::u32string(v);
    }

    constexpr void
    to_u32string(std::u32string_view v, std::u32string& str)
    {
        str = v;
    }

} // namespace m
