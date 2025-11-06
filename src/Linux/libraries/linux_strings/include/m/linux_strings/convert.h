// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <numeric>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/strings/convert.h>
#include <m/strings/tstring.h>
#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utf/transcode.h>
#include <m/utility/make_span.h>
#include <m/utility/pointers.h>
#include <m/utility/zstring.h>

namespace m
{
    namespace string_conversion_details
    {
        //
        template <>
        std::basic_string<char>
        string_view_to_string(std::basic_string_view<wchar_t> const&);
        template <>
        std::basic_string<char>
        string_view_to_string(std::basic_string_view<char8_t> const&);
        template <>
        std::basic_string<char>
        string_view_to_string(std::basic_string_view<char16_t> const&);
        template <>
        std::basic_string<char>
        string_view_to_string(std::basic_string_view<char32_t> const&);

        template <>
        std::basic_string<wchar_t>
        string_view_to_string(std::basic_string_view<char> const&);

        template <>
        std::basic_string<char8_t>
        string_view_to_string(std::basic_string_view<char> const&);
        template <>
        std::basic_string<char8_t>
        string_view_to_string(std::basic_string_view<wchar_t> const&);

        template <>
        std::basic_string<char16_t>
        string_view_to_string(std::basic_string_view<char> const&);
        template <>
        std::basic_string<char16_t>
        string_view_to_string(std::basic_string_view<wchar_t> const&);

        template <>
        std::basic_string<char32_t>
        string_view_to_string(std::basic_string_view<char> const&);
        template <>
        std::basic_string<char32_t>
        string_view_to_string(std::basic_string_view<wchar_t> const&);

        template <>
        std::basic_string<char>
        string_view_to_string(std::basic_string_view<wchar_t> const&);
        template <>
        std::basic_string<char>
        string_view_to_string(std::basic_string_view<char8_t> const&);
        template <>
        std::basic_string<char>
        string_view_to_string(std::basic_string_view<char16_t> const&);
        template <>
        std::basic_string<char>
        string_view_to_string(std::basic_string_view<char32_t> const&);

        template <>
        std::basic_string<wchar_t>
        string_to_string(std::basic_string<char> const&);
        template <>
        std::basic_string<wchar_t>
        string_to_string(std::basic_string<char8_t> const&);
        template <>
        std::basic_string<wchar_t>
        string_to_string(std::basic_string<char16_t> const&);
        template <>
        std::basic_string<wchar_t>
        string_to_string(std::basic_string<char32_t> const&);

        template <>
        std::basic_string<char8_t>
        string_to_string(std::basic_string<char> const&);
        template <>
        std::basic_string<char8_t>
        string_to_string(std::basic_string<wchar_t> const&);

        template <>
        std::basic_string<char16_t>
        string_to_string(std::basic_string<char> const&);
        template <>
        std::basic_string<char16_t>
        string_to_string(std::basic_string<wchar_t> const&);

        template <>
        std::basic_string<char32_t>
        string_to_string(std::basic_string<char> const&);
        template <>
        std::basic_string<char32_t>
        string_to_string(std::basic_string<wchar_t> const&);

    } // namespace string_conversion_details

} // namespace m
