// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <functional>
#include <iterator>
#include <numeric>
#include <utility>

#include <m/cast/to.h>
#include <m/linux_strings/convert.h>
#include <m/strings/convert.h>
#include <m/strings/tstring.h>
#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utf/transcode.h>
#include <m/utility/make_span.h>
#include <m/utility/pointers.h>
#include <m/utility/zstring.h>

namespace m
{
    std::u16string
    string_converter<wchar_t const*, std::u16string>::make_string(cwzstring str)
    {
        if (str == nullptr)
            return std::u16string();

        return string_converter<std::wstring_view, std::u16string>::make_string(
            std::wstring_view(str));
    }

    std::u16string
    string_converter<std::wstring_view, std::u16string>::make_string(std::wstring_view view)
    {
        return utf::transcode<char16_t>(view);
    }

    std::optional<std::u16string>
    string_converter<std::wstring_view, std::u16string>::make_string(
        std::optional<std::wstring_view> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return make_string(view.value());
    }

    std::u16string
    string_converter<std::wstring, std::u16string>::make_string(std::wstring const& str)
    {
        return string_converter<std::wstring_view, std::u16string>::make_string(
            static_cast<std::wstring_view>(str));
    }

    std::optional<std::u16string>
    string_converter<std::wstring, std::u16string>::make_string(
        std::optional<std::wstring> const& str)
    {
        if (!str.has_value())
            return std::nullopt;

        return make_string(str.value());
    }

} // namespace m
