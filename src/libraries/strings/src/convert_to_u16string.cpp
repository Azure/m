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

#if 0
    template <>
    struct string_converter<std::u8string, std::u16string, void>
    {
        static std::u16string
        make_string(std::u16string const& str)
        {
            std::u16string ret;
            utf::transcode(str.begin(), str.end(), m::string_inserter(ret));
            return ret;
        }

        static std::optional<std::u16string>
        make_string(std::optional<std::u16string> const& str)
        {
            if (!str.has_value())
                return std::nullopt;

            return make_string(str.value());
        }
    };

    template <>
    struct view_converter<std::u16string_view, std::u16string_view, void>
    {
        static std::u8string_view
        make_view(std::u8string_view view)
        {
            return view;
        }

        static std::optional<std::u8string_view>
        make_string(std::optional<std::u8string_view> const& view)
        {
            return view;
        }
    };

    template <>
    struct string_converter<std::u16string_view, std::u16string, void>
    {
        static std::u16string
        make_string(std::u16string_view view)
        {
            return std::u16string(view);
        }

        static std::optional<std::u16string>
        make_string(std::optional<std::u16string_view> const& view)
        {
            if (!view.has_value())
                return std::nullopt;

            return make_string(view.value());
        }
    };

    template <>
    struct string_converter<std::u16string, std::u16string, void>
    {
        static std::u16string
        make_string(std::u16string const& str)
        {
            std::u16string ret;
            utf::transcode(str.begin(), str.end(), m::string_inserter(ret));
            return ret;
        }

        static std::optional<std::u16string>
        make_string(std::optional<std::u16string> const& str)
        {
            if (!str.has_value())
                return std::nullopt;

            return make_string(str.value());
        }
    };

    template <>
    struct string_converter<std::u32string_view, std::u16string, void>
    {
        static std::u16string
        make_string(std::u32string_view view)
        {
            std::u16string ret;
            m::utf::transcode(view.begin(), view.end(), m::string_inserter(ret));
            return ret;
        }

        static std::optional<std::u16string>
        make_string(std::optional<std::u32string_view> const& view)
        {
            if (!view.has_value())
                return std::nullopt;

            return make_string(view.value());
        }
    };

    template <>
    struct string_converter<std::u32string, std::u16string, void>
    {
        static std::u16string
        make_string(std::u32string const& str)
        {
            std::u16string ret;
            utf::transcode(str.begin(), str.end(), m::string_inserter(ret));
            return ret;
        }

        static std::optional<std::u16string>
        make_string(std::optional<std::u32string> const& str)
        {
            if (!str.has_value())
                return std::nullopt;

            return make_string(str.value());
        }
    };
#endif
} // namespace m
