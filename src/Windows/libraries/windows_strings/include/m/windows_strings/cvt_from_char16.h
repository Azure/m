// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/multi_byte/convert.h>
#include <m/multi_byte/convert_acp.h>
#include <m/strings/string_conversion_details.h>

namespace m::string_conversion_details
{
    //
    // First we'll deal with equivalence classes
    //

    template <>
    struct sch<char16_t const*, std::wstring_view>
    {
        static std::wstring_view
        make_view(cu16zstring str);
    };

    template <>
    struct sch<std::u16string_view, std::wstring_view>
    {
        static std::wstring_view
        make_view(std::u16string_view view);

        static std::optional<std::wstring_view>
        make_view(std::optional<std::u16string_view> view);
    };

    template <>
    struct sch<std::u16string, std::wstring_view>
    {
        static std::wstring_view
        make_view(std::u16string const& str);

        static std::optional<std::wstring_view>
        make_view(std::optional<std::u16string> const& str);
    };

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
        make_string(std::u16string_view view);

        static std::optional<std::string>
        make_string(std::optional<std::u16string_view> const& view);
    };

    template <>
    struct sch<std::u16string, std::string>
    {
        static std::string
        make_string(std::u16string const& str);

        static std::optional<std::string>
        make_string(std::optional<std::u16string> const& str);
    };

    template <>
    struct sch<std::u16string_view, std::wstring>
    {
        static std::optional<std::wstring>
        make_string(std::optional<std::u16string_view> const& view);

        static std::wstring
        make_string(std::u16string_view view);
    };

    template <>
    struct sch<char16_t const*, std::wstring>
    {
        static std::wstring
        make_string(cu16zstring str);
    };

    template <>
    struct sch<std::u16string, std::wstring>
    {
        static std::optional<std::wstring>
        make_string(std::optional<std::u16string> const& s);

        static std::wstring
        make_string(std::u16string const& s);
    };

} // namespace m::string_conversion_details
