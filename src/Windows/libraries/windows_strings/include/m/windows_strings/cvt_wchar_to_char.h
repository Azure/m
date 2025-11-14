// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/multi_byte/convert.h>
#include <m/multi_byte/convert_acp.h>
#include <m/strings/string_conversion_details.h>

namespace m
{
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
        make_string(std::optional<std::wstring> const& str);

        static std::string
        make_string(std::wstring const& str);
    };
} // namespace m
