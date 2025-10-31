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
    template <>
    std::basic_string<char>
    string_view_to_string<char, char>(std::basic_string_view<char> const& from)
    {
        return std::basic_string<char>(from);
    }

    template <>
    std::basic_string<char>
    string_to_string<char, char>(std::basic_string<char> const& str)
    {
        return str;
    }
} // namespace m::string_conversion_details
