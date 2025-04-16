// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <string>
#include <string_view>

namespace m
{
    //
    // "safe" conversion of std::byte to wchar_t
    //
    wchar_t
    byte_to_wchar(std::byte b);

    //
    // to_wstring, across all the interesting input types
    //
    std::wstring
    to_wstring(std::string_view v);

    void
    to_wstring(std::string_view v, std::wstring& str);

    std::wstring
    to_wstring(std::wstring_view v);

    void
    to_wstring(std::wstring_view v, std::wstring& str);

    std::wstring
    to_wstring(std::u8string_view v);

    void
    to_wstring(std::u8string_view v, std::wstring& str);

    std::wstring
    to_wstring(std::u16string_view v);

    void
    to_wstring(std::u16string_view v, std::wstring& str);

    std::wstring
    to_wstring(std::u32string_view v);

    void
    to_wstring(std::u32string_view v, std::wstring& str);

    std::string
    to_string(std::string_view v);

    //
    // to_string
    //
    void
    to_string(std::string_view v, std::string& str);

    std::string
    to_string(std::wstring_view v);

    void
    to_string(std::wstring_view v, std::string& str);

    std::string
    to_string(std::u8string_view v);

    void
    to_string(std::u8string_view v, std::string& str);

    std::string
    to_string(std::u16string_view v);

    void
    to_string(std::u16string_view v, std::string& str);

    std::string
    to_string(std::u32string_view v);

    void
    to_string(std::u32string_view v, std::string& str);

    //
    // to_u8string
    //
    std::u8string
    to_u8string(std::string_view v);

    void
    to_u8string(std::string_view v, std::u8string& str);

    std::u8string
    to_u8string(std::wstring_view v);

    void
    to_u8string(std::wstring_view v, std::u8string& str);

    std::u8string
    to_u8string(std::u8string_view v);

    void
    to_u8string(std::u8string_view v, std::u8string& str);

    std::u8string
    to_u8string(std::u16string_view v);

    void
    to_u8string(std::u16string_view v, std::u8string& str);

    std::u8string
    to_u8string(std::u32string_view v);

    void
    to_u8string(std::u32string_view v, std::u8string& str);

    //
    // to_u16string
    //
    std::u16string
    to_u16string(std::string_view v);

    void
    to_u16string(std::string_view v, std::u16string& str);

    std::u16string
    to_u16string(std::wstring_view v);

    void
    to_u16string(std::wstring_view v, std::u16string& str);

    std::u16string
    to_u16string(std::u8string_view v);

    void
    to_u16string(std::u8string_view v, std::u16string& str);

    std::u16string
    to_u16string(std::u16string_view v);

    void
    to_u16string(std::u16string_view v, std::u16string& str);

    std::u16string
    to_u16string(std::u32string_view v);

    void
    to_u16string(std::u32string_view v, std::u16string& str);

    //
    // to_u32string
    //
    std::u32string
    to_u32string(std::string_view v);

    void
    to_u32string(std::string_view v, std::u32string& str);

    std::u32string
    to_u32string(std::wstring_view v);

    void
    to_u32string(std::wstring_view v, std::u32string& str);

    std::u32string
    to_u32string(std::u8string_view v);

    void
    to_u32string(std::u8string_view v, std::u32string& str);

    std::u32string
    to_u32string(std::u16string_view v);

    void
    to_u32string(std::u16string_view v, std::u32string& str);

    std::u32string
    to_u32string(std::u32string_view v);

    void
    to_u32string(std::u32string_view v, std::u32string& str);
} // namespace m
