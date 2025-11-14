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

namespace m
{
    std::u32string
    string_converter<std::u8string_view, std::u32string, void>::make_string(std::u8string_view view)
    {
        std::u32string ret;
        utf::transcode(view.begin(), view.end(), m::string_inserter(ret));
        return ret;
    }

    std::optional<std::u32string>
    string_converter<std::u8string_view, std::u32string, void>::make_string(
        std::optional<std::u8string_view> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return make_string(view.value());
    }

    std::u32string
    string_converter<std::u8string, std::u32string, void>::make_string(std::u8string const& str)
    {
        std::u32string ret;
        utf::transcode(str.begin(), str.end(), m::string_inserter(ret));
        return ret;
    }

    std::optional<std::u32string>
    string_converter<std::u8string, std::u32string, void>::make_string(
        std::optional<std::u8string> const& str)
    {
        if (!str.has_value())
            return std::nullopt;

        return make_string(str.value());
    }

    std::u32string
    string_converter<std::u16string_view, std::u32string, void>::make_string(
        std::u16string_view view)
    {
        std::u32string ret;
        m::utf::transcode(view.begin(), view.end(), m::string_inserter(ret));
        return ret;
    }

    std::optional<std::u32string>
    string_converter<std::u16string_view, std::u32string, void>::make_string(
        std::optional<std::u16string_view> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return make_string(view.value());
    }

    std::u32string
    string_converter<std::u16string, std::u32string, void>::make_string(std::u16string const& str)
    {
        std::u32string ret;
        utf::transcode(str.begin(), str.end(), m::string_inserter(ret));
        return ret;
    }

    std::optional<std::u32string>
    string_converter<std::u16string, std::u32string, void>::make_string(
        std::optional<std::u16string> const& str)
    {
        if (!str.has_value())
            return std::nullopt;

        return make_string(str.value());
    }

    std::u32string
    string_converter<std::u32string_view, std::u32string, void>::make_string(
        std::u32string_view view)
    {
        std::u32string ret;
        utf::transcode(view.begin(), view.end(), m::string_inserter(ret));
        return ret;
    }

    std::optional<std::u32string>
    string_converter<std::u32string_view, std::u32string, void>::make_string(
        std::optional<std::u32string_view> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return make_string(view.value());
    }

    std::u32string
    string_converter<std::u32string, std::u32string, void>::make_string(std::u32string const& str)
    {
        return utf::transcode<char32_t>(str);
    }

    std::optional<std::u32string>
    string_converter<std::u32string, std::u32string, void>::make_string(
        std::optional<std::u32string> const& str)
    {
        if (!str.has_value())
            return std::nullopt;

        return make_string(str.value());
    }
} // namespace m
