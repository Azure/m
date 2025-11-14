// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/multi_byte/convert.h>
#include <m/strings/convert.h>
#include <m/windows_strings/convert.h>

namespace m
{
    std::string
    string_converter<std::wstring_view, std::string>::make_string(std::wstring_view v)
    {
        std::string t;
        m::multi_byte::utf16_to_multi_byte(m::multi_byte::cp_acp, v, t);
        return t;
    }

    std::optional<std::string>
    string_converter<std::wstring_view, std::string>::make_string(
        std::optional<std::wstring_view> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return make_string(view.value());
    }

    std::string
    string_converter<std::wstring, std::string>::make_string(std::wstring const& s)
    {
        return string_converter<std::wstring_view, std::string>::make_string(view_of(s));
    }

    std::optional<std::string>
    string_converter<std::wstring, std::string>::make_string(std::optional<std::wstring> const& s)
    {
        if (!s.has_value())
            return std::nullopt;

        return make_string(s.value());
    }

} // namespace m
