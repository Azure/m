// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/multi_byte/convert.h>
#include <m/multi_byte/convert_acp.h>
#include <m/strings/conversion_details.h>

namespace m
{
    template <>
    struct string_converter<char8_t const*, std::wstring>
    {
        static std::wstring
        make_string(cu8zstring str);
    };

    template <>
    struct string_converter<std::u8string_view, std::wstring>
    {
        static std::optional<std::wstring>
        make_string(std::optional<std::u8string_view> const& view);

        static std::wstring
        make_string(std::u8string_view view);
    };

    template <>
    struct string_converter<std::u8string, std::wstring>
    {
        static std::optional<std::wstring>
        make_string(std::optional<std::u8string> const& str);

        static std::wstring
        make_string(std::u8string const& str);
    };

} // namespace m
