// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/multi_byte/convert.h>
#include <m/strings/convert.h>
#include <m/windows_strings/convert.h>

namespace m
{
    std::u16string
    string_converter<std::string_view, std::u16string>::make_string(std::string_view v)
    {
        std::u16string t;
        m::multi_byte_to_utf16(m::multi_byte::cp_acp, v, t);
        return t;
    }

    std::optional<std::u16string>
    string_converter<std::string_view, std::u16string>::make_string(
        std::optional<std::string_view> const& v)
    {
        if (!v.has_value())
            return std::nullopt;

        return make_string(v.value());
    }

    std::u16string
    string_converter<std::string, std::u16string>::make_string(std::string const& str)
    {
        return string_converter<std::string_view, std::u16string>::make_string(view_of(str));
    }

    std::optional<std::u16string>
    string_converter<std::string, std::u16string>::make_string(std::optional<std::string> const& s)
    {
        if (!s.has_value())
            return std::nullopt;

        return make_string(s.value());
    }

    std::u16string
    string_converter<char const*, std::u16string>::make_string(czstring str)
    {
        return string_converter<std::string_view, std::u16string>::make_string(view_of(str));
    }
} // namespace m
