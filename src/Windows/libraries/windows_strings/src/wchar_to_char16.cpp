// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <string>
#include <string_view>

#include <m/multi_byte/convert.h>
#include <m/strings/convert.h>
#include <m/utility/string_converter.h>
#include <m/utility/view_converter.h>
#include <m/windows_strings/convert.h>

//
// wchar_t to char16_t is platform specific, but it is not
// dependent on mbcs / CP_ACP. On Windows, wchar_t is UTF-16
// so this is strictly a transcoding exercise.
//

namespace m
{
    std::u16string
    string_converter<std::wstring_view, std::u16string>::make_string(std::wstring_view v)
    {
        return std::u16string(view_converter<std::wstring_view, std::u16string_view>::make_view(v));
    }

    std::optional<std::u16string>
    string_converter<std::wstring_view, std::u16string>::make_string(
        std::optional<std::wstring_view> const& v)
    {
        if (!v.has_value())
            return std::nullopt;

        return make_string(v.value());
    }

    std::u16string
    string_converter<wchar_t const*, std::u16string>::make_string(cwzstring str)
    {
        return std::u16string(std::u16string_view(reinterpret_cast<char16_t const*>(str)));
    }

    std::optional<std::u16string>
    string_converter<std::wstring, std::u16string>::make_string(
        std::optional<std::wstring> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return make_string(view.value());
    }

    std::u16string
    string_converter<std::wstring, std::u16string>::make_string(std::wstring const& str)
    {
        return string_converter<std::wstring_view, std::u16string>::make_string(
            std::wstring_view(str.begin(), str.end()));
    }

} // namespace m
