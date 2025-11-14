// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/multi_byte/convert.h>
#include <m/strings/convert.h>
#include <m/windows_strings/convert.h>

namespace m
{
    std::u32string
    string_converter<std::string_view, std::u32string>::make_string(std::string_view v)
    {
        //
        // There is no direct conversiono from CP_ACP to UTF-32 so
        // instead we must convert to wchar_t and then UTF-32
        //
        auto t = string_converter<decltype(v), std::wstring>::make_string(v);
        return string_converter<decltype(t), std::u32string>::make_string(t);
    }

    std::optional<std::u32string>
    string_converter<std::string_view, std::u32string>::make_string(
        std::optional<std::string_view> const& v)
    {
        if (!v.has_value())
            return std::nullopt;

        return make_string(v.value());
    }

    std::u32string
    string_converter<std::string, std::u32string>::make_string(std::string const& s)
    {
        return string_converter<std::string_view, std::u32string>::make_string(view_of(s));
    }

    std::optional<std::u32string>
    string_converter<std::string, std::u32string>::make_string(std::optional<std::string> const& s)
    {
        if (!s.has_value())
            return std::nullopt;

        return make_string(s.value());
    }

    std::u32string
    string_converter<char const*, std::u32string>::make_string(czstring str)
    {
        return string_converter<std::string_view, std::u32string>::make_string(view_of(str));
    }
} // namespace m
