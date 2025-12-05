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
#include <m/utility/string_converter.h>
#include <m/utility/view_converter.h>

namespace m
{
    std::u16string
    string_converter<std::u8string_view, std::u16string, void>::make_string(std::u8string_view view)
    {
        std::u16string ret;
        utf::transcode(view.begin(), view.end(), m::string_inserter(ret));
        return ret;
    }

    std::optional<std::u16string>
    string_converter<std::u8string_view, std::u16string, void>::make_string(
        std::optional<std::u8string_view> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return make_string(view.value());
    }
} // namespace m
