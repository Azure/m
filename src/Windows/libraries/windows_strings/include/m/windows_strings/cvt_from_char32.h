// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/multi_byte/convert.h>
#include <m/multi_byte/convert_acp.h>
#include <m/strings/string_conversion_details.h>

namespace m::string_conversion_details
{

    template <>
    struct sch<char32_t const*, std::string>
    {
        static std::string
        make_string(cu32zstring str);
    };

    template <>
    struct sch<std::u32string_view, std::string>
    {
        static std::optional<std::string>
        make_string(std::optional<std::u32string_view> const& view);

        static std::string
        make_string(std::u32string_view view);
    };

    template <>
    struct sch<char32_t const*, std::wstring>
    {
        static std::wstring
        make_string(cu32zstring str);
    };

    template <>
    struct sch<std::u32string_view, std::wstring>
    {
        static std::optional<std::wstring>
        make_string(std::optional<std::u32string_view> const& view);

        static std::wstring
        make_string(std::u32string_view view);
    };

    template <>
    struct sch<std::u32string, std::wstring>
    {
        static std::optional<std::wstring>
        make_string(std::optional<std::u32string> const& str);

        static std::wstring
        make_string(std::u32string const& str);
    };

} // namespace m::string_conversion_details
