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
// dependent on mbcs / CP_ACP. On Windows, wchar_t is UTF-16
// so this is strictly a transcoding exercise.
//

namespace m::string_conversion_details
{
    std::u32string
    sch<std::wstring_view, std::u32string>::make_string(std::wstring_view v)
    {
        return utf::transcode<char32_t>(v);
    }

    std::optional<std::u32string>
    sch<std::wstring_view, std::u32string>::make_string(std::optional<std::wstring_view> const& v)
    {
        if (!v.has_value())
            return std::nullopt;

        return make_string(v.value());
    }

    std::u32string
    sch<wchar_t const*, std::u32string>::make_string(cwzstring str)
    {
        return czstring_to_basic_string<char32_t>(str);
    }

    std::optional<std::u32string>
    sch<std::wstring, std::u32string>::make_string(std::optional<std::wstring> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return make_string(view.value());
    }

    std::u32string
    sch<std::wstring, std::u32string>::make_string(std::wstring const& str)
    {
        return sch<std::wstring_view, std::u32string>::make_string(
            std::wstring_view(str.begin(), str.end()));
    }

} // namespace m::string_conversion_details
