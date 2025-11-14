// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/multi_byte/convert.h>
#include <m/strings/convert.h>
#include <m/windows_strings/convert.h>

namespace m
{
    std::wstring
    string_converter<std::string_view, std::wstring>::make_string(std::string_view v)
    {
        std::wstring t;
        m::multi_byte::multi_byte_to_utf16(m::multi_byte::cp_acp, v, t);
        return t;
    }

    std::optional<std::wstring>
    string_converter<std::string_view, std::wstring>::make_string(
        std::optional<std::string_view> const& v)
    {
        if (!v.has_value())
            return std::nullopt;

        return make_string(v.value());
    }

    std::wstring
    string_converter<std::string, std::wstring>::make_string(std::string const& s)
    {
        return string_converter<std::string_view, std::wstring>::make_string(
            std::string_view(s.begin(), s.end()));
    }

    std::optional<std::wstring>
    string_converter<std::string, std::wstring>::make_string(std::optional<std::string> const& s)
    {
        if (!s.has_value())
            return std::nullopt;

        return make_string(s.value());
    }

    std::wstring
    string_converter<char const*, std::wstring>::make_string(czstring str)
    {
        return string_converter<std::string_view, std::wstring>::make_string(
            string_converter<decltype(str), std::string_view>::make_view(str));
    }
} // namespace m
