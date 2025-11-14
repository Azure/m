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
    std::wstring
    sch<std::u16string_view, std::wstring>::make_string(std::u16string_view v)
    {
        return utf::transcode<wchar_t>(v);
    }

    std::optional<std::wstring>
    sch<std::u16string_view, std::wstring>::make_string(std::optional<std::u16string_view> const& v)
    {
        if (!v.has_value())
            return std::nullopt;

        return make_string(v.value());
    }

    std::wstring
    sch<char16_t const*, std::wstring>::make_string(cu16zstring str)
    {
        if (str == nullptr)
            return std::wstring();

        return sch<std::u16string_view, std::wstring>::make_string(std::u16string_view(str));
    }

    std::optional<std::wstring>
    sch<std::u16string, std::wstring>::make_string(std::optional<std::u16string> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return make_string(view.value());
    }

    std::wstring
    sch<std::u16string, std::wstring>::make_string(std::u16string const& str)
    {
        return sch<std::u16string_view, std::wstring>::make_string(
            std::u16string_view(str.begin(), str.end()));
    }

} // namespace m::string_conversion_details
