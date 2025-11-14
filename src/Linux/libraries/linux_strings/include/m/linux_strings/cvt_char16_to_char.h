// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <m/strings/string_conversion_details.h>

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
        static std::string
        make_string(std::u16string_view v);

        static std::optional<std::string>
        make_string(std::optional<std::u16string_view> const& v);
    };

    template <>
    struct string_converter<std::u16string, std::string>
    {
        static std::string
        make_string(std::u16string const& s);

        static std::optional<std::string>
        make_string(std::optional<std::u16string> const& s);
    };

} // namespace m
