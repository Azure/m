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
    std::u16string
    string_converter<std::u32string_view, std::u16string>::make_string(std::u32string_view view)
    {
        return utf::transcode<char16_t>(view);
    }

    std::optional<std::u16string>
    string_converter<std::u32string_view, std::u16string>::make_string(
        std::optional<std::u32string_view> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return make_string(view.value());
    }

    std::u16string
    string_converter<std::u32string, std::u16string>::make_string(std::u32string const& str)
    {
        return utf::transcode<char16_t>(str);
    }

    std::optional<std::u16string>
    string_converter<std::u32string, std::u16string>::make_string(std::optional<std::u32string> const& str)
    {
        if (!str.has_value())
            return std::nullopt;

        return make_string(str.value());
    }
} // namespace m::conversion_details
