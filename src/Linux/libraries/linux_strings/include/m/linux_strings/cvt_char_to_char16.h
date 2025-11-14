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
        struct sch<char const*, std::u16string>
        {
            static std::u16string
            make_string(czstring str);
        };

        template <>
        struct sch<std::string_view, std::u16string>
        {
            static std::u16string
            make_string(std::string_view v);

            static std::optional<std::u16string>
            make_string(std::optional<std::string_view> const& v);
        };

        template <>
        struct sch<std::string, std::u16string>
        {
            static std::u16string
            make_string(std::string const& s);

            static std::optional<std::u16string>
            make_string(std::optional<std::string> const& s);
        };

    } // namespace string_conversion_details

} // namespace m
