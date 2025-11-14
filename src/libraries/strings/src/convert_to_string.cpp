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
#include <m/utility/zstring.h>

namespace m::string_conversion_details
{
    std::string_view
    sch<std::string_view, std::string_view>::make_view(std::string_view view)
    {
        return view;
    }

    std::optional<std::string_view>
    sch<std::string_view, std::string_view>::make_string(
        std::optional<std::string_view> const& view)
    {
        return view;
    }

    std::string
    sch<std::string_view, std::string>::make_string(std::string_view view)
    {
        return std::string(view);
    }

    std::optional<std::string>
    sch<std::string_view, std::string>::make_string(std::optional<std::string_view> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return std::string(view.value());
    }

    std::string
    sch<std::string, std::string>::make_string(std::string const& str)
    {
        return str;
    }

    std::optional<std::string>
    sch<std::string, std::string>::make_string(std::optional<std::string> const& str)
    {
        return str;
    }

    std::string
    sch<char const*, std::string>::make_string(not_null<czstring> str)
    {
        return std::string(str);
    }

} // namespace m::string_conversion_details
