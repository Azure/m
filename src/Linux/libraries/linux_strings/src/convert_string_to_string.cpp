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

namespace
{
    template <typename ToCharT, typename FromCharT>
        requires(m::character<ToCharT> && m::character<FromCharT>)
    std::basic_string<ToCharT>
    transcode_view_to(std::basic_string_view<FromCharT> const& view)
    {
        std::basic_string<ToCharT> to{};
        m::utf::transcode(view, to);
        return to;
    }
} // namespace

namespace m::string_conversion_details
{
    template <>
    std::basic_string<char>
    string_to_string(std::basic_string<wchar_t> const& from)
    {
        return transcode_view_to<char, wchar_t>(std::basic_string_view<wchar_t>(from));
    }

    template <>
    std::basic_string<char>
    string_to_string(std::basic_string<char8_t> const& from)
    {
        return transcode_view_to<char, char8_t>(std::u8string_view(from));
    }

    template <>
    std::basic_string<char>
    string_to_string(std::basic_string<char16_t> const& from)
    {
        return transcode_view_to<char, char16_t>(std::u16string_view(from));
    }

    template <>
    std::basic_string<char>
    string_to_string(std::basic_string<char32_t> const& from)
    {
        return transcode_view_to<char, char32_t>(std::u32string_view(from));
    }

    template <>
    std::basic_string<wchar_t>
    string_to_string(std::basic_string<char> const& from)
    {
        return transcode_view_to<wchar_t, char>(std::basic_string_view<char>(from));
    }

    template <>
    std::basic_string<char8_t>
    string_to_string(std::basic_string<char> const& from)
    {
        return transcode_view_to<char8_t, char>(std::basic_string_view<char>(from));
    }

    template <>
    std::basic_string<char16_t>
    string_to_string(std::basic_string<char> const& from)
    {
        return transcode_view_to<char16_t, char>(std::basic_string_view<char>(from));
    }

    template <>
    std::basic_string<char32_t>
    string_to_string(std::basic_string<char> const& from)
    {
        return transcode_view_to<char32_t, char>(std::basic_string_view<char>(from));
    }

} // namespace m::string_conversion_details
