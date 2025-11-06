// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <m/strings/convert.h>
#include <m/strings/string_conversion_details.h>
#include <m/strings/tstring.h>
#include <m/utf/transcode.h>
#include <m/utility/pointers.h>

#include "convert.h"

//
// This header defines functions in the m namespace which will provide
// fluent conversion between the strongly typed UTF-8/UTF-16/UTF-32 and
// both char on the assumption that char is encoded using CP_ACP and
// wchar_t on the assumption that wchar_t is encoded as UTF-16.
//
// These are, more or less, the tacit assumptions on Windows.
//

namespace m
{
    namespace string_conversion_details
    {
        template <>
        std::basic_string<char>
        string_view_to_string<wchar_t, char>(std::basic_string_view<wchar_t> const&);

        template <>
        std::basic_string<char>
        string_view_to_string<char8_t, char>(std::basic_string_view<char8_t> const&);

        template <>
        std::basic_string<char>
        string_view_to_string<char16_t, char>(std::basic_string_view<char16_t> const&);

        template <>
        std::basic_string<char>
        string_view_to_string<char32_t, char>(std::basic_string_view<char32_t> const&);

        template <>
        std::basic_string<wchar_t>
        string_view_to_string<char, wchar_t>(std::basic_string_view<char> const&);

        template <>
        std::basic_string<wchar_t>
        string_view_to_string<char8_t, wchar_t>(std::basic_string_view<char8_t> const&);

        template <>
        std::basic_string<wchar_t>
        string_view_to_string<char16_t, wchar_t>(std::basic_string_view<char16_t> const&);

        template <>
        std::basic_string<wchar_t>
        string_view_to_string<char32_t, wchar_t>(std::basic_string_view<char32_t> const&);


        template <>
        std::basic_string<char8_t>
        string_to_string<char, char8_t>(std::basic_string<char> const&);

        template <>
        std::basic_string<char8_t>
        string_to_string<wchar_t, char8_t>(std::basic_string<wchar_t> const&);

        template <>
        std::basic_string<char16_t>
        string_to_string<char, char16_t>(std::basic_string<char> const&);

        template <>
        std::basic_string<char16_t>
        string_to_string<wchar_t, char16_t>(std::basic_string<wchar_t> const&);

        template <>
        std::basic_string<char32_t>
        string_to_string<char, char32_t>(std::basic_string<char> const&);

        template <>
        std::basic_string<char32_t>
        string_to_string<wchar_t, char32_t>(std::basic_string<wchar_t> const&);

        template <>
        std::basic_string<char>
        string_to_string<wchar_t, char>(std::basic_string<wchar_t> const&);

        template <>
        std::basic_string<char>
        string_to_string<char8_t, char>(std::basic_string<char8_t> const&);

        template <>
        std::basic_string<char>
        string_to_string<char16_t, char>(std::basic_string<char16_t> const&);

        template <>
        std::basic_string<char>
        string_to_string<char32_t, char>(std::basic_string<char32_t> const&);

        template <>
        std::basic_string<wchar_t>
        string_to_string<char, wchar_t>(std::basic_string<char> const&);

        template <>
        std::basic_string<wchar_t>
        string_to_string<char8_t, wchar_t>(std::basic_string<char8_t> const&);

        template <>
        std::basic_string<wchar_t>
        string_to_string<char16_t, wchar_t>(std::basic_string<char16_t> const&);

        template <>
        std::basic_string<wchar_t>
        string_to_string<char32_t, wchar_t>(std::basic_string<char32_t> const&);

        template <>
        std::basic_string<char8_t>
        string_to_string<char, char8_t>(std::basic_string<char> const&);

        template <>
        std::basic_string<char8_t>
        string_to_string<wchar_t, char8_t>(std::basic_string<wchar_t> const&);

        template <>
        std::basic_string<char16_t>
        string_to_string<char, char16_t>(std::basic_string<char> const&);

        template <>
        std::basic_string<char16_t>
        string_to_string<wchar_t, char16_t>(std::basic_string<wchar_t> const&);

        template <>
        std::basic_string<char32_t>
        string_to_string<char, char32_t>(std::basic_string<char> const&);

        template <>
        std::basic_string<char32_t>
        string_to_string<wchar_t, char32_t>(std::basic_string<wchar_t> const&);




    } // namespace string_conversion_details
} // namespace m
