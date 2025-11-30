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
    struct string_converter<wchar_t const*, std::string>
    {
        static std::string
        make_string(cwzstring str);
    };

    template <>
    struct string_converter<std::wstring_view, std::string>
    {
        static std::string
        make_string(std::wstring_view v);

        static std::optional<std::string>
        make_string(std::optional<std::wstring_view> const& v);
    };

    template <>
    struct string_converter<std::wstring, std::string>
    {
        static std::string
        make_string(std::wstring const& s);

        static std::optional<std::string>
        make_string(std::optional<std::wstring> const& s);
    };
} // namespace m
