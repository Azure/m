// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/multi_byte/convert.h>
#include <m/strings/conversion_details.h>

namespace m
{
    template <>
    struct string_converter<char32_t const*, std::string>
    {
        static std::string
        make_string(cu32zstring str);
    };

    template <>
    struct string_converter<std::u32string_view, std::string>
    {
        static std::optional<std::string>
        make_string(std::optional<std::u32string_view> const& view);

        static std::string
        make_string(std::u32string_view view);
    };

    template <>
    struct string_converter<std::u32string, std::string>
    {
        static std::optional<std::string>
        make_string(std::optional<std::u32string> const& str);

        static std::string
        make_string(std::u32string const& str);
    };
} // namespace m
