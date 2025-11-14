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
        struct sch<char16_t const*, std::string>
        {
            static std::string
            make_string(cu16zstring str);
        };

        template <>
        struct sch<std::u16string_view, std::string>
        {
            static std::string
            make_string(std::u16string_view v);

            static std::optional<std::string>
            make_string(std::optional<std::u16string_view> const& v);
        };

        template <>
        struct sch<std::u16string, std::string>
        {
            static std::string
            make_string(std::u16string const& s);

            static std::optional<std::string>
            make_string(std::optional<std::u16string> const& s);
        };

    } // namespace string_conversion_details

} // namespace m
