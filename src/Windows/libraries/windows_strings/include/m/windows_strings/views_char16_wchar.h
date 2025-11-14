// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/multi_byte/convert.h>
#include <m/multi_byte/convert_acp.h>
#include <m/strings/string_conversion_details.h>

namespace m
{
    template <>
    struct string_converter<wchar_t const*, std::u16string_view>
    {
        static std::u16string_view
        make_view(cwzstring str);
    };

    template <>
    struct string_converter<std::wstring_view, std::u16string_view>
    {
        static std::u16string_view
        make_view(std::wstring_view view);

        static std::optional<std::u16string_view>
        make_view(std::optional<std::wstring_view> const& view);
    };

    template <>
    struct string_converter<std::wstring, std::u16string_view>
    {
        static std::u16string_view
        make_view(std::wstring const& str);

        static std::optional<std::u16string_view>
        make_view(std::optional<std::wstring> const& str);
    };

    template <>
    struct string_converter<m::wsstring, std::u16string_view>
    {
        static std::u16string_view
        make_view(m::wsstring str);

        static std::optional<std::u16string_view>
        make_view(std::optional<m::wsstring> const& str);
    };

    template <>
    struct string_converter<char16_t const*, std::wstring_view>
    {
        static std::wstring_view
        make_view(cu16zstring str);
    };

    template <>
    struct string_converter<std::u16string_view, std::wstring_view>
    {
        static std::wstring_view
        make_view(std::u16string_view view);

        static std::optional<std::wstring_view>
        make_view(std::optional<std::u16string_view> const& view);
    };

    template <>
    struct string_converter<std::u16string, std::wstring_view>
    {
        static std::wstring_view
        make_view(std::u16string const& str);

        static std::optional<std::wstring_view>
        make_view(std::optional<std::u16string> const& str);
    };

    template <>
    struct string_converter<m::u16sstring, std::wstring_view>
    {
        static std::wstring_view
        make_view(m::u16sstring str);

        static std::optional<std::wstring_view>
        make_view(std::optional<m::u16sstring> const& str);
    };

} // namespace m