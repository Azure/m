// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <functional>
#include <iterator>
#include <numeric>
#include <utility>

#include <m/cast/to.h>
#include <m/linux_strings/convert.h>
#include <m/linux_strings/cvt_char_to_char8.h>
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
    std::wstring
    string_converter<char32_t const*, std::wstring>::make_string(cu32zstring str)
    {
        if (str == nullptr)
            return std::wstring();

        return string_converter<std::u32string_view, std::wstring>::make_string(
            std::u32string_view(str));
    }

    std::wstring
    string_converter<std::u32string_view, std::wstring>::make_string(std::u32string_view v)
    {
        return std::wstring(
            std::wstring_view(reinterpret_cast<wchar_t const*>(v.data()), v.size()));
    }

    std::optional<std::wstring>
    string_converter<std::u32string_view, std::wstring>::make_string(
        std::optional<std::u32string_view> const& v)
    {
        if (!v.has_value())
            return std::nullopt;

        return make_string(v.value());
    }

    std::wstring
    string_converter<std::u32string, std::wstring>::make_string(std::u32string const& str)
    {
        return string_converter<std::u32string_view, std::wstring>::make_string(
            static_cast<std::u32string_view>(str));
    }

    std::optional<std::wstring>
    string_converter<std::u32string, std::wstring>::make_string(
        std::optional<std::u32string> const& str)
    {
        if (!str.has_value())
            return std::nullopt;

        return make_string(str.value());
    }

} // namespace m
