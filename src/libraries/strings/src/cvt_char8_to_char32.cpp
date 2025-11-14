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

namespace m
{
    std::u32string
    string_converter<std::u8string_view, std::u32string>::make_string(std::u8string_view view)
    {
        return utf::transcode<char32_t>(view);
    }

    std::optional<std::u32string>
    string_converter<std::u8string_view, std::u32string>::make_string(
        std::optional<std::u8string_view> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return make_string(view.value());
    }

    std::u32string
    string_converter<std::u8string, std::u32string>::make_string(std::u8string const& str)
    {
        return string_converter<std::u8string_view, std::u32string>::make_string(static_cast<std::u8string_view>(str));
    }

    std::optional<std::u32string>
    string_converter<std::u8string, std::u32string>::make_string(std::optional<std::u8string> const& str)
    {
        if (!str.has_value())
            return std::nullopt;

        return make_string(str.value());
    }
} // namespace m::string_conversion_details
