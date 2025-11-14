// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <functional>
#include <iterator>
#include <numeric>
#include <utility>

#include <m/cast/to.h>
#include <m/linux_strings/convert.h>
#include <m/linux_strings/cvt_char_to_char8.h>
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
    std::optional<std::u8string>
    sch<std::string_view, std::u8string>::make_string(std::optional<std::string_view> const& v)
    {
        if (!v.has_value())
            return std::nullopt;

        return make_string(v.value());
    }

    std::u8string
    sch<std::string_view, std::u8string>::make_string(std::string_view v)
    {
        return utf::transcode<char8_t>(v);
    }

    std::u8string
    sch<std::string, std::u8string>::make_string(std::string const& str)
    {
        return utf::transcode<char8_t>(str);
    }

    std::optional<std::u8string>
    sch<std::string, std::u8string>::make_string(std::optional<std::string> const& v)
    {
        if (!v.has_value())
            return std::nullopt;

        return make_string(v.value());
    }

} // namespace m::string_conversion_details
