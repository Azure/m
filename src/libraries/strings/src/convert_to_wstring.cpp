// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <functional>
#include <iterator>
#include <numeric>
#include <utility>

#include <m/cast/to.h>
#include <m/strings/convert.h>
#include <m/utf/decode.h>
#include <m/utf/encode.h>

namespace m::string_conversion_details
{
    std::wstring_view
    sch<wchar_t const*, std::wstring_view>::make_view(not_null<cwzstring> str)
    {
        return std::wstring_view(str);
    }

    std::optional<std::wstring_view>
    sch<wchar_t const*, std::wstring_view>::make_view(cwzstring str)
    {
        if (str == nullptr)
            return std::nullopt;

        return std::wstring_view(str);
    }

    std::wstring
    sch<wchar_t const*, std::wstring>::make_string(not_null<cwzstring> str)
    {
        return std::wstring(str);
    }

    std::optional<std::wstring>
    sch<wchar_t const*, std::wstring>::make_string(cwzstring str)
    {
        if (str == nullptr)
            return std::nullopt;

        return std::wstring(str);
    }

    std::wstring
    sch<std::wstring_view, std::wstring>::make_string(std::wstring_view view)
    {
        return std::wstring(view);
    }

    std::optional<std::wstring>
    sch<std::wstring_view, std::wstring>::make_string(std::optional<std::wstring_view> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return std::wstring(view.value());
    }

    std::wstring
    sch<std::wstring, std::wstring>::make_string(std::wstring const& str)
    {
        return str;
    }

    std::optional<std::wstring>
    sch<std::wstring, std::wstring>::make_string(std::optional<std::wstring> const& str)
    {
        return str;
    }

} // namespace m::string_conversion_details
