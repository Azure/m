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
    std::string
    sch<wchar_t const*, std::string>::make_string(cwzstring str)
    {
        if (str == nullptr)
            return std::string();

        return sch<std::wstring_view, std::string>::make_string(std::wstring_view(str));
    }

    std::string
    sch<std::wstring_view, std::string>::make_string(std::wstring_view view)
    {
        return utf::transcode<char>(view);
    }

    std::optional<std::string>
    sch<std::wstring_view, std::string>::make_string(std::optional<std::wstring_view> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return make_string(view.value());
    }

    std::string
    sch<std::wstring, std::string>::make_string(std::wstring const& str)
    {
        return sch<std::wstring_view, std::string>::make_string(static_cast<std::wstring_view>(str));
    }

    std::optional<std::string>
    sch<std::wstring, std::string>::make_string(std::optional<std::wstring> const& str)
    {
        if (!str.has_value())
            return std::nullopt;

        return make_string(str.value());
    }

} // namespace m::string_conversion_details
