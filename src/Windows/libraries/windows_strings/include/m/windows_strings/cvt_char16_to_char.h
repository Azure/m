// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/multi_byte/convert.h>
#include <m/multi_byte/convert_acp.h>
#include <m/strings/conversion_details.h>

namespace m
{
    template <>
    struct string_converter<char16_t const*, std::string>
    {
        static std::string
        make_string(cu16zstring str);
    };

    template <>
    struct string_converter<std::u16string_view, std::string>
    {
        static std::optional<std::string>
        make_string(std::optional<std::u16string_view> const& view);

        static std::string
        make_string(std::u16string_view view);
    };

    template <>
    struct string_converter<std::u16string, std::string>
    {
        static std::optional<std::string>
        make_string(std::optional<std::u16string> const& str);

        static std::string
        make_string(std::u16string const& str);
    };
} // namespace m
