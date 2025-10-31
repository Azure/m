// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <functional>
#include <iterator>
#include <numeric>
#include <utility>

#include <m/cast/to.h>
#include <m/linux_strings/convert.h>
#include <m/strings/convert.h>
#include <m/strings/tstring.h>
#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utf/transcode.h>
#include <m/utility/make_span.h>
#include <m/utility/pointers.h>
#include <m/utility/zstring.h>

namespace m::string_conversion_details
{
    template <typename ToCharT, typename FromT>
        requires(m::character<ToCharT>)
    std::basic_string<ToCharT>
    transcode_to(FromT&& from)
    {
        std::basic_string<ToCharT> to{};
        utf::transcode(std::forward<FromT>(from), to);
        return to;
    }

    template <>
    std::basic_string<char>
    string_view_to_string(std::basic_string_view<wchar_t> const& from)
    {
        return transcode_to<char>(from);
    }

    template <>
    std::basic_string<char>
    string_view_to_string(std::basic_string_view<char8_t> const& from)
    {
        return transcode_to<char>(from);
    }

    template <>
    std::basic_string<char>
    string_view_to_string(std::basic_string_view<char16_t> const& from)
    {
        return transcode_to<char>(from);
    }

    template <>
    std::basic_string<char>
    string_view_to_string(std::basic_string_view<char32_t> const& from)
    {
        return transcode_to<char>(from);
    }

    template <>
    std::basic_string<wchar_t>
    string_view_to_string(std::basic_string_view<char> const& from)
    {
        return transcode_to<wchar_t>(from);
    }

    template <>
    std::basic_string<char8_t>
    string_view_to_string(std::basic_string_view<char> const& from)
    {
        return transcode_to<char8_t>(from);
    }

    template <>
    std::basic_string<char16_t>
    string_view_to_string(std::basic_string_view<char> const& from)
    {
        return transcode_to<char16_t>(from);
    }

    template <>
    std::basic_string<char32_t>
    string_view_to_string(std::basic_string_view<char> const& from)
    {
        return transcode_to<char32_t>(from);
    }
} // namespace m::string_conversion_details
