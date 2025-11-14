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
    string_converter<std::u16string_view, std::u32string>::make_string(std::u16string_view view)
    {
        return utf::transcode<char32_t>(view);
    }

    std::optional<std::u32string>
    string_converter<std::u16string_view, std::u32string>::make_string(
        std::optional<std::u16string_view> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return make_string(view.value());
    }

    std::u32string
    string_converter<std::u16string, std::u32string>::make_string(std::u16string const& str)
    {
        return utf::transcode<char32_t>(str);
    }

    std::optional<std::u32string>
    string_converter<std::u16string, std::u32string>::make_string(std::optional<std::u16string> const& str)
    {
        if (!str.has_value())
            return std::nullopt;

        return make_string(str.value());
    }

} // namespace m::string_conversion_details
