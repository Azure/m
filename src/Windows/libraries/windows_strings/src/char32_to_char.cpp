// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <string>
#include <string_view>

#include <m/multi_byte/convert.h>
#include <m/strings/convert.h>
#include <m/windows_strings/convert.h>

//
// char to char32_t is platform specific, but it is not
// dependent on mbcs / CP_ACP. On Windows, char is UTF-32
// so this is strictly a transcoding exercise.
//

namespace m
{
    std::string
    string_converter<std::u32string_view, std::string>::make_string(std::u32string_view v)
    {
        return utf::transcode<char>(v);
    }

    std::optional<std::string>
    string_converter<std::u32string_view, std::string>::make_string(
        std::optional<std::u32string_view> const& v)
    {
        if (!v.has_value())
            return std::nullopt;

        return make_string(v.value());
    }

    std::string
    string_converter<char32_t const*, std::string>::make_string(cu32zstring str)
    {
        return string_converter<std::u32string_view, std::string>::make_string(view_of(str));
    }

    std::optional<std::string>
    string_converter<std::u32string, std::string>::make_string(
        std::optional<std::u32string> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return make_string(view.value());
    }

    std::string
    string_converter<std::u32string, std::string>::make_string(std::u32string const& str)
    {
        return string_converter<std::u32string_view, std::string>::make_string(view_of(str));
    }

} // namespace m
