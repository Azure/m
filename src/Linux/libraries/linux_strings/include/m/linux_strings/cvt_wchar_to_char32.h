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
        struct sch<wchar_t const*, std::u32string>
        {
            static std::u32string
            make_string(cwzstring str);
        };

        template <>
        struct sch<std::wstring_view, std::u32string>
        {
            static std::u32string
            make_string(std::wstring_view v);

            static std::optional<std::u32string>
            make_string(std::optional<std::wstring_view> const& v);
        };

        template <>
        struct sch<std::wstring, std::u32string>
        {
            static std::u32string
            make_string(std::wstring const& s);

            static std::optional<std::u32string>
            make_string(std::optional<std::wstring> const& s);
        };

    } // namespace string_conversion_details

} // namespace m
