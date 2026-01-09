// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/multi_byte/convert.h>
#include <m/sstring/sstring.h>
#include <m/strings/conversion_details.h>

namespace m
{
    template <>
    struct string_converter<char const*, std::u16string>
    {
        static std::u16string
        make_string(czstring str);
    };

    template <>
    struct string_converter<std::string_view, std::u16string>
    {
        static std::optional<std::u16string>
        make_string(std::optional<std::string_view> const& view);

        static std::u16string
        make_string(std::string_view view);
    };

    template <>
    struct string_converter<std::string, std::u16string>
    {
        static std::optional<std::u16string>
        make_string(std::optional<std::string> const& view);

        static std::u16string
        make_string(std::string const& str);
    };
} // namespace m
