// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/multi_byte/convert.h>
#include <m/strings/convert.h>
#include <m/windows_strings/convert.h>

namespace m::string_conversion_details
{
    std::string
    sch<std::wstring_view, std::string>::make_string(std::wstring_view v)
    {
        std::string t;
        m::multi_byte::utf16_to_multi_byte(m::multi_byte::cp_acp, v, t);
        return t;
    }

    std::string
        sch<std::wstring, std::string>::make_string(std::wstring const& s)
    {
        return sch<std::wstring_view, std::string>::make_string(
            std::wstring_view(s.begin(), s.end()));
    }

} // namespace m::string_conversion_details
