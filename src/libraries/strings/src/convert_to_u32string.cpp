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
    std::basic_string<char32_t>
    string_view_to_string<wchar_t, char32_t>(std::basic_string_view<wchar_t> const& from)
    {
        std::u32string to;
        utf::transcode(from, to);
        return to;
    }

    template <>
    std::basic_string<char32_t>
    string_to_string<wchar_t, char32_t>(std::basic_string<wchar_t> const& str)
    {
        return string_view_to_string<wchar_t, char32_t>(std::wstring_view(str));
    }

    template <>
    std::basic_string<char32_t>
    string_view_to_string<char8_t, char32_t>(std::basic_string_view<char8_t> const& from)
    {
        std::u32string to;
        utf::transcode(from, to);
        return to;
    }

    template <>
    std::basic_string<char32_t>
    string_to_string<char8_t, char32_t>(std::basic_string<char8_t> const& str)
    {
        return string_view_to_string<char8_t, char32_t>(std::u8string_view(str));
    }

    template <>
    std::basic_string<char32_t>
    string_view_to_string<char16_t, char32_t>(std::basic_string_view<char16_t> const& from)
    {
        std::u32string to;
        utf::transcode(from, to);
        return to;
    }

    template <>
    std::basic_string<char32_t>
    string_to_string<char16_t, char32_t>(std::basic_string<char16_t> const& str)
    {
        return string_view_to_string<char16_t, char32_t>(std::u16string_view(str));
    }


    template <>
    std::basic_string<char32_t>
    string_view_to_string<char32_t, char32_t>(std::basic_string_view<char32_t> const& from)
    {
        return std::basic_string<char32_t>(from);
    }

    template <>
    std::basic_string<char32_t>
    string_to_string<char32_t, char32_t>(std::basic_string<char32_t> const& str)
    {
        return str;
    }

} // namespace m::string_conversion_details
