// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <functional>
#include <iterator>
#include <numeric>
#include <utility>

#include <m/utility/string_converter.h>

#include <m/cast/to.h>
#include <m/strings/convert.h>
#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utility/string_converter.h>
#include <m/utility/view_converter.h>
#include <m/utility/zstring.h>

namespace m
{
    std::string_view
    view_converter<std::string_view, std::string_view, void>::make_view(std::string_view view)
    {
        return view;
    }

    std::optional<std::string_view>
    string_converter<std::string_view, std::string_view, void>::make_string(
        std::optional<std::string_view> const& view)
    {
        return view;
    }

    std::string
    string_converter<std::string_view, std::string, void>::make_string(std::string_view view)
    {
        return std::string(view);
    }

    std::optional<std::string>
    string_converter<std::string_view, std::string, void>::make_string(
        std::optional<std::string_view> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return std::string(view.value());
    }

    std::string
    string_converter<std::string, std::string, void>::make_string(std::string const& str)
    {
        return str;
    }

    std::optional<std::string>
    string_converter<std::string, std::string, void>::make_string(
        std::optional<std::string> const& str)
    {
        return str;
    }

    std::string
    string_converter<char const*, std::string, void>::make_string(not_null<czstring> str)
    {
        return std::string(str);
    }

} // namespace m
