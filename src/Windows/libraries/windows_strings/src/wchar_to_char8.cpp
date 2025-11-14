// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <string>
#include <string_view>

#include <m/multi_byte/convert.h>
#include <m/strings/convert.h>
#include <m/windows_strings/convert.h>

//
// wchar_t to char8_t is platform specific, but it is not
// dependent on mbcs / CP_ACP. On Windows, wchar_t is UTF-16
// so this is strictly a transcoding exercise.
//

namespace m
{
    std::u8string
    string_converter<std::wstring_view, std::u8string>::make_string(std::wstring_view v)
    {
        return utf::transcode<char8_t>(v);
    }

    std::optional<std::u8string>
    string_converter<std::wstring_view, std::u8string>::make_string(
        std::optional<std::wstring_view> const& v)
    {
        if (!v.has_value())
            return std::nullopt;

        return make_string(v.value());
    }

    std::u8string
    string_converter<wchar_t const*, std::u8string>::make_string(cwzstring str)
    {
        return string_converter<std::wstring_view, std::u8string>::make_string(view_of(str));
    }

    std::optional<std::u8string>
    string_converter<std::wstring, std::u8string>::make_string(
        std::optional<std::wstring> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return make_string(view.value());
    }

    std::u8string
    string_converter<std::wstring, std::u8string>::make_string(std::wstring const& str)
    {
        return string_converter<std::wstring_view, std::u8string>::make_string(view_of(str));
    }

} // namespace m
