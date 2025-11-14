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

namespace m
{
    std::wstring
    string_converter<char8_t const*, std::wstring>::make_string(cu8zstring str)
    {
        if (str == nullptr)
            return std::wstring();

        return string_converter<std::u8string_view, std::wstring>::make_string(
            std::u8string_view(str));
    }

    std::wstring
    string_converter<std::u8string_view, std::wstring>::make_string(std::u8string_view view)
    {
        return utf::transcode<wchar_t>(view);
    }

    std::optional<std::wstring>
    string_converter<std::u8string_view, std::wstring>::make_string(
        std::optional<std::u8string_view> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return make_string(view.value());
    }

    std::wstring
    string_converter<std::u8string, std::wstring>::make_string(std::u8string const& str)
    {
        return string_converter<std::u8string_view, std::wstring>::make_string(
            static_cast<std::u8string_view>(str));
    }

    std::optional<std::wstring>
    string_converter<std::u8string, std::wstring>::make_string(
        std::optional<std::u8string> const& str)
    {
        if (!str.has_value())
            return std::nullopt;

        return make_string(str.value());
    }

} // namespace m
