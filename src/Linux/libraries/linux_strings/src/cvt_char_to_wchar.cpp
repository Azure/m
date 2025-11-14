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
    sch<char const*, std::wstring>::make_string(czstring str)
    {
        if (str == nullptr)
            return std::wstring();

        return sch<std::string_view, std::wstring>::make_string(std::string_view(str));
    }

    std::wstring
    sch<std::string_view, std::wstring>::make_string(std::string_view view)
    {
        return utf::transcode<wchar_t>(view);
    }

    std::optional<std::wstring>
    sch<std::string_view, std::wstring>::make_string(std::optional<std::string_view> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return make_string(view.value());
    }

    std::wstring
    sch<std::string, std::wstring>::make_string(std::string const& str)
    {
        return sch<std::string_view, std::wstring>::make_string(static_cast<std::string_view>(str));
    }

    std::optional<std::wstring>
    sch<std::string, std::wstring>::make_string(std::optional<std::string> const& str)
    {
        if (!str.has_value())
            return std::nullopt;

        return make_string(str.value());
    }

} // namespace m::string_conversion_details
