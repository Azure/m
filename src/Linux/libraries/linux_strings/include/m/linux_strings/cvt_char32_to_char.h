// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <m/strings/string_conversion_details.h>

namespace m
{
    namespace string_conversion_details
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
            static std::string
            make_string(std::u32string_view v);

            static std::optional<std::string>
            make_string(std::optional<std::u32string_view> const& v);
        };

        template <>
        struct sch<std::u32string, std::string>
        {
            static std::string
            make_string(std::u32string const& s);

            static std::optional<std::string>
            make_string(std::optional<std::u32string> const& s);
        };

    } // namespace string_conversion_details

} // namespace m
