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

#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utf/transcode.h>
#include <m/utility/make_span.h>

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

} // namespace m
