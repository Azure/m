// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/multi_byte/convert.h>
#include <m/sstring/sstring.h>
#include <m/strings/conversion_details.h>

namespace m
{
    template <>
    struct string_converter<wchar_t const*, std::u16string>
    {
        static std::u16string
        make_string(cwzstring str);
    };

    template <>
    struct string_converter<std::wstring_view, std::u16string>
    {
        static std::optional<std::u16string>
        make_string(std::optional<std::wstring_view> const& view);

        static std::u16string
        make_string(std::wstring_view view);
    };

    template <>
    struct string_converter<std::wstring, std::u16string>
    {
        static std::optional<std::u16string>
        make_string(std::optional<std::wstring> const& view);

        static std::u16string
        make_string(std::wstring const& str);
    };
} // namespace m
