// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <string>
#include <string_view>

#include <m/multi_byte/convert.h>
#include <m/strings/convert.h>
#include <m/windows_strings/convert.h>

//
// wchar_t to char32_t is platform specific, but it is not
// dependent on mbcs / CP_ACP. On Windows, wchar_t is UTF-32
// so this is strictly a transcoding exercise.
//

namespace m
{
    std::wstring
    string_converter<std::u32string_view, std::wstring>::make_string(std::u32string_view v)
    {
        return utf::transcode<wchar_t>(v);
    }

    std::optional<std::wstring>
    string_converter<std::u32string_view, std::wstring>::make_string(
        std::optional<std::u32string_view> const& v)
    {
        if (!v.has_value())
            return std::nullopt;

        return make_string(v.value());
    }

    std::wstring
    string_converter<char32_t const*, std::wstring>::make_string(cu32zstring str)
    {
        return string_converter<std::u32string_view, std::wstring>::make_string(view_of(str));
        // return czstring_to_basic_string<wchar_t>(str);
    }

    std::optional<std::wstring>
    string_converter<std::u32string, std::wstring>::make_string(
        std::optional<std::u32string> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return make_string(view.value());
    }

    std::wstring
    string_converter<std::u32string, std::wstring>::make_string(std::u32string const& str)
    {
        return string_converter<std::u32string_view, std::wstring>::make_string(
            std::u32string_view(str.begin(), str.end()));
    }

} // namespace m
