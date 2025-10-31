// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <functional>
#include <iterator>
#include <numeric>
#include <utility>

#include <m/cast/to.h>
#include <m/strings/convert.h>
#include <m/utf/decode.h>
#include <m/utf/encode.h>

namespace m::string_conversion_details
{
    template <>
    std::basic_string<wchar_t>
    string_view_to_string<wchar_t, wchar_t>(std::basic_string_view<wchar_t> const& from)
    {
        return std::basic_string<wchar_t>(from);
    }

    template <>
    std::basic_string<wchar_t>
    string_to_string<wchar_t, wchar_t>(std::basic_string<wchar_t> const& str)
    {
        return str;
    }

    template <>
    std::basic_string<wchar_t>
    string_view_to_string<char8_t, wchar_t>(std::basic_string_view<char8_t> const& from)
    {
        std::basic_string<wchar_t> to;
        utf::transcode(from, to);
        return to;
    }

    template <>
    std::basic_string<wchar_t>
    string_to_string<char8_t, wchar_t>(std::basic_string<char8_t> const& str)
    {
        return string_view_to_string<char8_t, wchar_t>(std::basic_string_view<char8_t>(str));
    }

    template <>
    std::basic_string<wchar_t>
    string_view_to_string<char16_t, wchar_t>(std::basic_string_view<char16_t> const& from)
    {
        std::basic_string<wchar_t> to;
        utf::transcode(from, to);
        return to;
    }

    template <>
    std::basic_string<wchar_t>
    string_to_string<char16_t, wchar_t>(std::basic_string<char16_t> const& str)
    {
        return string_view_to_string<char16_t, wchar_t>(std::basic_string_view<char16_t>(str));
    }

    template <>
    std::basic_string<wchar_t>
    string_view_to_string<char32_t, wchar_t>(std::basic_string_view<char32_t> const& from)
    {
        std::basic_string<wchar_t> to;
        utf::transcode(from, to);
        return to;
    }

    template <>
    std::basic_string<wchar_t>
    string_to_string<char32_t, wchar_t>(std::basic_string<char32_t> const& str)
    {
        return string_view_to_string<char32_t, wchar_t>(std::basic_string_view<char32_t>(str));
    }

} // namespace m::string_conversion_details
