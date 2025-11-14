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
    struct string_converter<char32_t const*, std::wstring>
    {
        static std::wstring
        make_string(cu32zstring str);
    };

    template <>
    struct string_converter<std::u32string_view, std::wstring>
    {
        static std::wstring
        make_string(std::u32string_view v);

        static std::optional<std::wstring>
        make_string(std::optional<std::u32string_view> const& v);
    };

    template <>
    struct string_converter<std::u32string, std::wstring>
    {
        static std::wstring
        make_string(std::u32string const& s);

        static std::optional<std::wstring>
        make_string(std::optional<std::u32string> const& s);
    };
} // namespace m
