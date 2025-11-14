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
    std::u16string
    sch<std::u16string_view, std::u16string>::make_string(std::u16string_view view)
    {
        return std::u16string(view);
    }

    template <>
    std::optional<std::u16string>
    sch<std::u16string_view, std::u16string>::make_string(
        std::optional<std::u16string_view> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return make_string(view.value());
    }

    template <>
    std::u16string
    sch<std::u16string, std::u16string>::make_string(std::u16string const& str)
    {
        return str;
    }

    template <>
    std::optional<std::u16string>
    sch<std::u16string, std::u16string>::make_string(std::optional<std::u16string> const& str)
    {
        return str;
    }

} // namespace m::string_conversion_details
