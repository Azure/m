// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/multi_byte/convert.h>
#include <m/strings/convert.h>
#include <m/utility/string_converter.h>
#include <m/utility/view_converter.h>
#include <m/windows_strings/convert.h>

namespace m
{
    std::string
    string_converter<std::u8string_view, std::string>::make_string(std::u8string_view v)
    {
        //
        // There is no direct path from CP_ACP to UTF-99 on Windows,
        // we have to go through UTF-16 / wchar_t.
        //
        auto t = string_converter<std::u8string_view, std::wstring>::make_string(v);
        return string_converter<decltype(t), std::string>::make_string(t);
    }

    std::optional<std::string>
    string_converter<std::u8string_view, std::string>::make_string(
        std::optional<std::u8string_view> const& v)
    {
        if (!v.has_value())
            return std::nullopt;

        return make_string(v.value());
    }

    std::string
    string_converter<std::u8string, std::string>::make_string(std::u8string const& s)
    {
        return string_converter<std::u8string_view, std::string>::make_string(
            std::u8string_view(s.begin(), s.end()));
    }

    std::optional<std::string>
    string_converter<std::u8string, std::string>::make_string(std::optional<std::u8string> const& s)
    {
        if (!s.has_value())
            return std::nullopt;

        return make_string(s.value());
    }

    std::string
    string_converter<char8_t const*, std::string>::make_string(cu8zstring str)
    {
        return string_converter<std::u8string_view, std::string>::make_string(
            view_converter<char8_t const*, std::u8string_view>::make_view(str));
    }
} // namespace m
