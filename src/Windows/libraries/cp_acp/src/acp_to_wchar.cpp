// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/cast/to.h>
#include <m/multi_byte/convert.h>
#include <m/utility/make_span.h>

#include <m/cp_acp/convert.h>

#include <Windows.h>

namespace m
{
    std::size_t
    acp_to_wchar_length(std::string_view in)
    {
        return acp_to_utf16_length(in);
    }

    std::size_t
    acp_to_wchar_length(std::string_view in, std::error_code& ec)
    {
        return acp_to_utf16_length(in, ec);
    }

    template <>
    void
    acp_to_span(std::string_view in, std::span<wchar_t>& out)
    {
        acp_to_utf16(in, out);
    }

    template <>
    void
    acp_to_span(std::string_view in, std::span<wchar_t>& out, std::error_code& ec)
    {
        acp_to_utf16(in, out, ec);
    }

    void
    acp_to_wstring(std::string_view in, std::wstring& out)
    {
        acp_to_basic_string(in, out);
    }

    void
    acp_to_wstring(std::string_view in, std::wstring& out, std::error_code& ec)
    {
        acp_to_basic_string(in, out, ec);
    }

    std::wstring
    acp_to_wstring(std::string_view in)
    {
        std::wstring out;
        acp_to_wstring(in, out);
        return out;
    }

    std::wstring
    acp_to_wstring(std::string_view in, std::error_code& ec)
    {
        std::wstring out;
        acp_to_wstring(in, out, ec);
        return out;
    }
} // namespace m