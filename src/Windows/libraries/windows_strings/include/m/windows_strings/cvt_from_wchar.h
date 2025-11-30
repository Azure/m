// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/multi_byte/convert.h>
#include <m/multi_byte/convert_acp.h>
#include <m/strings/conversion_details.h>

namespace m
{
    template <>
    struct string_converter<wchar_t const*, std::u8string>
    {
        static std::u8string
        make_string(cwzstring str);
    };

    template <>
    struct string_converter<std::wstring_view, std::u8string>
    {
        static std::optional<std::u8string>
        make_string(std::optional<std::wstring_view> const& view);

        static std::u8string
        make_string(std::wstring_view view);
    };

    template <>
    struct string_converter<std::wstring, std::u8string>
    {
        static std::optional<std::u8string>
        make_string(std::optional<std::wstring> const& view);

        static std::u8string
        make_string(std::wstring const& str);
    };

    template <>
    struct string_converter<wchar_t const*, std::u16string>
    {
        static std::u16string
        make_string(cwzstring str);
    };

    template <>
    struct string_converter<std::wstring_view, std::u16string>
    {
        static std::optional<std::u16string>
        make_string(std::optional<std::wstring_view> const& view);

        static std::u16string
        make_string(std::wstring_view view);
    };

    template <>
    struct string_converter<std::wstring, std::u16string>
    {
        static std::optional<std::u16string>
        make_string(std::optional<std::wstring> const& view);

        static std::u16string
        make_string(std::wstring const& str);
    };

    template <>
    struct string_converter<wchar_t const*, std::u32string>
    {
        static std::u32string
        make_string(cwzstring str);
    };

    template <>
    struct string_converter<std::wstring_view, std::u32string>
    {
        static std::optional<std::u32string>
        make_string(std::optional<std::wstring_view> const& view);

        static std::u32string
        make_string(std::wstring_view view);
    };

    template <>
    struct string_converter<std::wstring, std::u32string>
    {
        static std::optional<std::u32string>
        make_string(std::optional<std::wstring> const& view);

        static std::u32string
        make_string(std::wstring const& str);
    };

    template <>
    struct string_converter<wchar_t const*, std::string>
    {
        static std::string
        make_string(cwzstring str);
    };

    template <>
    struct string_converter<std::wstring_view, std::string>
    {
        static std::optional<std::string>
        make_string(std::optional<std::wstring_view> const& view);

        static std::string
        make_string(std::wstring_view view);
    };

    template <>
    struct string_converter<std::wstring, std::string>
    {
        static std::optional<std::string>
        make_string(std::optional<std::wstring> const& view);

        static std::string
        make_string(std::wstring const& str);
    };

} // namespace m
