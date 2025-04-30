// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/cast/to.h>
#include <m/multi_byte/convert.h>
#include <m/utility/make_span.h>

#include <Windows.h>

namespace m
{
    std::string
    to_acp_string(std::wstring_view view)
    {
        return to_string(multi_byte::cp_acp, view);
    }

    void
    to_acp_string(std::wstring_view view, std::string& str)
    {
        to_string(multi_byte::cp_acp, view, str);
    }

    std::string
    to_acp_string(std::u8string_view view)
    {
        return to_string(multi_byte::cp_acp, view);
    }

    void
    to_acp_string(std::u8string_view view, std::string& str)
    {
        to_string(multi_byte::cp_acp, view, str);
    }

    std::string
    to_acp_string(std::u16string_view view)
    {
        return to_string(multi_byte::cp_acp, view);
    }

    void
    to_acp_string(std::u16string_view view, std::string& str)
    {
        to_string(multi_byte::cp_acp, view, str);
    }

    std::string
    to_acp_string(std::u32string_view view)
    {
        return to_string(multi_byte::cp_acp, view);
    }

    void
    to_acp_string(std::u32string_view view, std::string& str)
    {
        to_string(multi_byte::cp_acp, view, str);
    }
} // namespace m

