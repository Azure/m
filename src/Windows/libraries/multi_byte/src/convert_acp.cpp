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
    void
    to_string(std::u8string_view v, std::string& str)
    {
        to_acp_string(v, str);
    }

    std::string
    to_string(std::u8string_view v)
    {
        return to_acp_string(v);
    }

    void
    to_string(std::u16string_view v, std::string& str)
    {
        to_acp_string(v, str);
    }

    std::string
    to_string(std::u16string_view v)
    {
        return to_acp_string(v);
    }

    void
    to_string(std::u32string_view v, std::string& str)
    {
        to_acp_string(v, str);
    }

    std::string
    to_string(std::u32string_view v)
    {
        return to_acp_string(v);
    }

    void
    to_string(std::wstring_view v, std::string& str)
    {
        to_acp_string(v, str);
    }

    std::string
    to_string(std::wstring_view v)
    {
        return to_acp_string(v);
    }

    void
    to_u8string(std::string_view v, std::u8string& str)
    {
        acp_to_u8string(v, str);
    }

    std::u8string
    to_u8string(std::string_view v)
    {
        return acp_to_u8string(v);
    }

    void
    to_u16string(std::string_view v, std::u16string& str)
    {
        acp_to_u16string(v, str);
    }

    std::u16string
    to_u16string(std::string_view v)
    {
        return acp_to_u16string(v);
    }

    void
    to_u32string(std::string_view v, std::u32string& str)
    {
        acp_to_u32string(v, str);
    }

    std::u32string
    to_u32string(std::string_view v)
    {
        return acp_to_u32string(v);
    }

    void
    to_wstring(std::string_view v, std::wstring& str)
    {
        acp_to_wstring(v, str);
    }

    std::wstring
    to_wstring(std::string_view v)
    {
        return acp_to_wstring(v);
    }

    //
} // namespace m
