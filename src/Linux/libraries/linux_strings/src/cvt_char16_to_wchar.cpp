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

namespace m::string_conversion_details
{
    std::wstring
    sch<char16_t const*, std::wstring>::make_string(cu16zstring str)
    {
        if (str == nullptr)
            return std::wstring();

        return sch<std::u16string_view, std::wstring>::make_string(std::u16string_view(str));
    }

    std::wstring
    sch<std::u16string_view, std::wstring>::make_string(std::u16string_view view)
    {
        return utf::transcode<wchar_t>(view);
    }

    std::optional<std::wstring>
    sch<std::u16string_view, std::wstring>::make_string(
        std::optional<std::u16string_view> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return make_string(view.value());
    }

    std::wstring
    sch<std::u16string, std::wstring>::make_string(std::u16string const& str)
    {
        return sch<std::u16string_view, std::wstring>::make_string(
            static_cast<std::u16string_view>(str));
    }

    std::optional<std::wstring>
    sch<std::u16string, std::wstring>::make_string(std::optional<std::u16string> const& str)
    {
        if (!str.has_value())
            return std::nullopt;

        return make_string(str.value());
    }

} // namespace m::string_conversion_details
