// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/multi_byte/convert.h>
#include <m/strings/convert.h>
#include <m/windows_strings/convert.h>

namespace m::string_conversion_details
{
    std::u16string
    sch<std::string_view, std::u16string>::make_string(std::string_view v)
    {
        std::u16string t;
        m::multi_byte::multi_byte_to_utf16(m::multi_byte::cp_acp, v, t);
        return t;
    }

    std::optional<std::u16string>
    sch<std::string_view, std::u16string>::make_string(std::optional<std::string_view> const& v)
    {
        if (!v.has_value())
            return std::nullopt;

        return make_string(v.value());
    }

    std::u16string
    sch<std::string, std::u16string>::make_string(std::string const& s)
    {
        return sch<std::string_view, std::u16string>::make_string(
            std::string_view(s.begin(), s.end()));
    }

    std::optional<std::u16string>
    sch<std::string, std::u16string>::make_string(std::optional<std::string> const& s)
    {
        if (!s.has_value())
            return std::nullopt;

        return make_string(s.value());
    }

    std::u16string
    sch<char const*, std::u16string>::make_string(czstring str)
    {
        return czstring_to_basic_string<char16_t>(str);
    }
} // namespace m::string_conversion_details
