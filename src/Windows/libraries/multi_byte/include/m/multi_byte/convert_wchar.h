// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <string>
#include <string_view>

#include <m/utf/transcode.h>

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

} // namespace m
