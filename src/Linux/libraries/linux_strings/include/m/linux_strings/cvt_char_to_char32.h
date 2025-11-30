// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <m/strings/conversion_details.h>
#include <m/strings/string_conversions.h>

namespace m
{
        template <>
        struct string_converter<char const*, std::u32string>
        {
            static std::u32string
            make_string(czstring str);
        };

        template <>
        struct string_converter<std::string_view, std::u32string>
        {
            static std::u32string
            make_string(std::string_view v);

            static std::optional<std::u32string>
            make_string(std::optional<std::string_view> const& v);
        };

        template <>
        struct string_converter<std::string, std::u32string>
        {
            static std::u32string
            make_string(std::string const& s);

            static std::optional<std::u32string>
            make_string(std::optional<std::string> const& s);
        };
} // namespace m
