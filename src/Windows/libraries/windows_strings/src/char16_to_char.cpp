// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/multi_byte/convert.h>
#include <m/strings/convert.h>
#include <m/windows_strings/convert.h>

namespace m
{
    std::string
    string_converter<std::u16string_view, std::string>::make_string(std::u16string_view v)
    {
        std::string t;
        m::multi_byte::utf16_to_multi_byte(m::multi_byte::cp_acp, v, t);
        return t;
    }

    std::optional<std::string>
    string_converter<std::u16string_view, std::string>::make_string(
        std::optional<std::u16string_view> const& v)
    {
        if (!v.has_value())
            return std::nullopt;

        return make_string(v.value());
    }

    std::string
    string_converter<std::u16string, std::string>::make_string(std::u16string const& s)
    {
        return string_converter<std::u16string_view, std::string>::make_string(
            std::u16string_view(s.begin(), s.end()));
    }

    std::optional<std::string>
    string_converter<std::u16string, std::string>::make_string(
        std::optional<std::u16string> const& v)
    {
        if (!v.has_value())
            return std::nullopt;

        return make_string(v.value());
    }
} // namespace m
