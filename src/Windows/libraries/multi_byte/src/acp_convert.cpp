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
        std::wstring str;
        acp_to_wstring(view, str);
        return str;
    }

    void
    acp_to_wstring(std::string_view view, std::wstring& str)
    {
        to_wstring(multi_byte::cp_acp, view, str);
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
} // namespace m

namespace m::multi_byte::impl
{
    void
    acp_convert(std::string_view v, std::wstring& str)
    {
        acp_to_wstring(v, str);
    }

    void
    acp_convert(std::string_view v, std::u8string& str)
    {
        acp_to_u8string(v, str);
    }

    void
    acp_convert(std::string_view v, std::u16string& str)
    {
        acp_to_u16string(v, str);
    }

    void
    acp_convert(std::string_view v, std::u32string& str)
    {
        acp_to_u32string(v, str);
    }

    void
    acp_convert(std::wstring_view v, std::string& str)
    {
        to_acp_string(v, str);
    }

    void
    acp_convert(std::u8string_view v, std::string& str)
    {
        to_acp_string(v, str);
    }

    void
    acp_convert(std::u16string_view v, std::string& str)
    {
        to_acp_string(v, str);
    }

    void
    acp_convert(std::u32string_view v, std::string& str)
    {
        to_acp_string(v, str);
    }

} // namespace m::multi_byte::impl