// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/cast/to.h>
#include <m/multi_byte/convert.h>
#include <m/multi_byte/convert_acp.h>
#include <m/strings/convert.h>
#include <m/utility/make_span.h>

#include "acp_convert.h"

namespace m::string_conversion_details
{
    using m::multi_byte::impl::acp_convert;

    template <>
    std::basic_string<char>
    string_view_to_string<wchar_t, char>(std::basic_string_view<wchar_t> const& from)
    {
        std::basic_string<char> to;
        acp_convert(from, to);
        return to;
    }

    template <>
    std::basic_string<char>
    string_to_string<wchar_t, char>(std::basic_string<wchar_t> const& str)
    {
        return string_view_to_string<wchar_t, char>(std::basic_string_view<wchar_t>(str));
    }

    template <>
    std::basic_string<char>
    string_view_to_string<char8_t, char>(std::basic_string_view<char8_t> const& from)
    {
        std::basic_string<char> to;
        acp_convert(from, to);
        return to;
    }

    template <>
    std::basic_string<char>
    string_to_string<char8_t, char>(std::basic_string<char8_t> const& str)
    {
        return string_view_to_string<char8_t, char>(std::basic_string_view<char8_t>(str));
    }

    template <>
    std::basic_string<char>
    string_view_to_string<char16_t, char>(std::basic_string_view<char16_t> const& from)
    {
        std::basic_string<char> to;
        acp_convert(from, to);
        return to;
    }

    template <>
    std::basic_string<char>
    string_to_string<char16_t, char>(std::basic_string<char16_t> const& str)
    {
        return string_view_to_string<char16_t, char>(std::basic_string_view<char16_t>(str));
    }

    template <>
    std::basic_string<char>
    string_view_to_string<char32_t, char>(std::basic_string_view<char32_t> const& from)
    {
        std::basic_string<char> to;
        acp_convert(from, to);
        return to;
    }

    template <>
    std::basic_string<char>
    string_to_string<char32_t, char>(std::basic_string<char32_t> const& str)
    {
        return string_view_to_string<char32_t, char>(std::basic_string_view<char32_t>(str));
    }

    template <>
    std::basic_string<wchar_t>
    string_view_to_string<char, wchar_t>(std::basic_string_view<char> const& from)
    {
        std::basic_string<wchar_t> to;
        acp_convert(from, to);
        return to;
    }

    template <>
    std::basic_string<wchar_t>
    string_to_string<char, wchar_t>(std::basic_string<char> const& str)
    {
        return string_view_to_string<char, wchar_t>(std::basic_string_view<char>(str));
    }

    template <>
    std::basic_string<char8_t>
    string_view_to_string<char, char8_t>(std::basic_string_view<char> const& from)
    {
        std::basic_string<char8_t> to;
        acp_convert(from, to);
        return to;
    }

    template <>
    std::basic_string<char8_t>
    string_to_string<char, char8_t>(std::basic_string<char> const& str)
    {
        return string_view_to_string<char, char8_t>(std::basic_string_view<char>(str));
    }

    template <>
    std::basic_string<char16_t>
    string_view_to_string<char, char16_t>(std::basic_string_view<char> const& from)
    {
        std::basic_string<char16_t> to;
        acp_convert(from, to);
        return to;
    }

    template <>
    std::basic_string<char16_t>
    string_to_string<char, char16_t>(std::basic_string<char> const& str)
    {
        return string_view_to_string<char, char16_t>(std::basic_string_view<char>(str));
    }

    template <>
    std::basic_string<char32_t>
    string_view_to_string<char, char32_t>(std::basic_string_view<char> const& from)
    {
        std::basic_string<char32_t> to;
        acp_convert(from, to);
        return to;
    }

    template <>
    std::basic_string<char32_t>
    string_to_string<char, char32_t>(std::basic_string<char> const& str)
    {
        return string_view_to_string<char, char32_t>(std::basic_string_view<char>(str));
    }




} // namespace m::string_conversion_details
