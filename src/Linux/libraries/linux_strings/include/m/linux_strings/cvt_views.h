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
        struct sch<char8_t const*, std::string_view>
        {
            static std::string_view
            make_view(cu8zstring str);
        };

        template <>
        struct sch<std::u8string_view, std::string_view>
        {
            static std::string_view
            make_view(std::u8string_view view);

            static std::optional<std::string_view>
            make_view(std::optional<std::u8string_view> const& view);
        };

        template <>
        struct sch<char const*, std::u8string_view>
        {
            static std::u8string_view
            make_view(czstring str);
        };

        template <>
        struct sch<std::string_view, std::u8string_view>
        {
            static std::u8string_view
            make_view(std::string_view view);

            static std::optional<std::u8string_view>
            make_view(std::optional<std::string_view> const& view);
        };

        template <>
        struct sch<wchar_t const*, std::u32string_view>
        {
            static std::u32string_view
            make_view(cwzstring str);
        };

        template <>
        struct sch<std::wstring_view, std::u32string_view>
        {
            static std::u32string_view
            make_view(std::wstring_view view);

            static std::optional<std::u32string_view>
            make_view(std::optional<std::wstring_view> const& view);
        };

        template <>
        struct sch<char32_t const*, std::wstring_view>
        {
            static std::wstring_view
            make_view(cu32zstring str);
        };

        template <>
        struct sch<std::u32string_view, std::wstring_view>
        {
            static std::wstring_view
            make_view(std::u32string_view view);

            static std::optional<std::wstring_view>
            make_view(std::optional<std::u32string_view> const& view);
        };

    } // namespace string_conversion_details

} // namespace m
