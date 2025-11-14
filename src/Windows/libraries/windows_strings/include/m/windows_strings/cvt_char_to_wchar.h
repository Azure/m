// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/multi_byte/convert.h>
#include <m/multi_byte/convert_acp.h>
#include <m/strings/string_conversion_details.h>

namespace m
{
    template <>
    struct string_converter<char const*, std::wstring>
    {
        static std::wstring
        make_string(czstring str);
    };

    template <>
    struct string_converter<std::string_view, std::wstring>
    {
        static std::optional<std::wstring>
        make_string(std::optional<std::string_view> const& view);

        static std::wstring
        make_string(std::string_view view);
    };

    template <>
    struct string_converter<std::string, std::wstring>
    {
        static std::optional<std::wstring>
        make_string(std::optional<std::string> const& view);

        static std::wstring
        make_string(std::string const& str);
    };
} // namespace m
