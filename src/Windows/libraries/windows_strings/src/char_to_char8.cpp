// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/multi_byte/convert.h>
#include <m/strings/convert.h>
#include <m/windows_strings/convert.h>

namespace m::string_conversion_details
{
    std::u8string
    sch<std::string_view, std::u8string>::make_string(std::string_view v)
    {
        //
        // There is no direct path from CP_ACP to UTF-8 on Windows,
        // we have to go through UTF-16 / wchar_t.
        //
        auto t = sch<std::string_view, std::wstring>::make_string(v);
        return sch<decltype(t), std::u8string>::make_string(t);
    }

    std::optional<std::u8string>
    sch<std::string_view, std::u8string>::make_string(std::optional<std::string_view> const& v)
    {
        if (!v.has_value())
            return std::nullopt;

        return make_string(v.value());
    }

    std::u8string
    sch<std::string, std::u8string>::make_string(std::string const& s)
    {
        return sch<std::string_view, std::u8string>::make_string(
            std::string_view(s.begin(), s.end()));
    }

    std::optional<std::u8string>
    sch<std::string, std::u8string>::make_string(std::optional<std::string> const& s)
    {
        if (!s.has_value())
            return std::nullopt;

        return make_string(s.value());
    }

    std::u8string
    sch<char const*, std::u8string>::make_string(czstring str)
    {
        return sch<std::string_view, std::u8string>::make_string(
            sch<decltype(str), std::string_view>::make_view(str));
    }
} // namespace m::string_conversion_details
