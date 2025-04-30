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

    //
    // acp_to_wstring
    //
    std::wstring
    acp_to_wstring(std::string_view view)
    {
        return to_wstring(multi_byte::cp_acp, view);
    }

    void
    acp_to_wstring(std::string_view view, std::wstring& str)
    {
        acp_to_wstring(view, str);
    }

    void
    acp_to_u8string(std::string_view v, std::u8string& str)
    {
        to_u8string(multi_byte::cp_acp, v, str);
    }

    std::u8string
    acp_to_u8string(std::string_view v)
    {
        std::u8string str;
        acp_to_u8string(v, str);
        return str;
    }

    void
    acp_to_u16string(std::string_view v, std::u16string& str)
    {
        to_u16string(multi_byte::cp_acp, v, str);
    }

    std::u16string
    acp_to_u16string(std::string_view v)
    {
        std::u16string str;
        acp_to_u16string(v, str);
        return str;
    }

    void
    acp_to_u32string(std::string_view v, std::u32string& str)
    {
        to_u32string(multi_byte::cp_acp, v, str);
    }

    std::u32string
    acp_to_u32string(std::string_view v)
    {
        std::u32string str;
        acp_to_u32string(v, str);
        return str;
    }

    //
} // namespace m
