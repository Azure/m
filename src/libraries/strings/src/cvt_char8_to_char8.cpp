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
    std::u8string
    sch<std::u8string_view, std::u8string>::make_string(std::u8string_view view)
    {
        return std::u8string(view);
    }

    template <>
    std::optional<std::u8string>
    sch<std::u8string_view, std::u8string>::make_string(
        std::optional<std::u8string_view> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return std::u8string(view.value());
    }

    template <>
    std::u8string
    sch<std::u8string, std::u8string>::make_string(std::u8string const& str)
    {
        return utf::transcode<char8_t>(str);
    }

    template <>
    std::optional<std::u8string>
    sch<std::u8string, std::u8string>::make_string(std::optional<std::u8string> const& str)
    {
        if (!str.has_value())
            return std::nullopt;

        return make_string(str.value());
    }

} // namespace m::string_conversion_details
