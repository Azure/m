// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/cast/to.h>
#include <m/multi_byte/convert.h>
#include <m/utf/transcode.h>
#include <m/utility/make_span.h>

#include <m/cp_acp/convert.h>

#include <Windows.h>

namespace m
{
    //
    // acp_to_* - char flavor
    //

    template <>
    std::size_t
    acp_to_length(std::string_view in, char const&)
    {
        return acp_to_char_length(in);
    }

    template <>
    std::size_t
    acp_to_length(std::string_view in, char const&, std::error_code& ec)
    {
        return acp_to_char_length(in, ec);
    }

    template <>
    void
    acp_to_basic_string(std::string_view in, std::string& out)
    {
        out = in;
    }

    template <>
    void
    acp_to_basic_string(std::string_view in, std::string& out, std::error_code& ec)
    {
        out = in;
    }

    template <>
    std::string
    acp_to_basic_string(std::string_view in)
    {
        return std::string(in);
    }

    template <>
    std::string
    acp_to_basic_string(std::string_view in, std::error_code& ec)
    {
        return std::string(in);
    }

    //
    // acp_to_* - wchar_t flavor
    //

    template <>
    std::size_t
    acp_to_length(std::string_view in, wchar_t const&)
    {
        return acp_to_wchar_length(in);
    }

    template <>
    std::size_t
    acp_to_length(std::string_view in, wchar_t const&, std::error_code& ec)
    {
        return acp_to_wchar_length(in, ec);
    }

    template <>
    void
    acp_to_basic_string(std::string_view in, std::wstring& out)
    {
        multi_byte_to_utf16(multi_byte::cp_acp, in, out);
    }

    template <>
    void
    acp_to_basic_string(std::string_view in, std::wstring& out, std::error_code& ec)
    {
        multi_byte_to_utf16(multi_byte::cp_acp, in, out, ec);
    }

    template <>
    std::wstring
    acp_to_basic_string(std::string_view in)
    {
        std::wstring out;
        multi_byte_to_utf16(multi_byte::cp_acp, in, out);
        return out;
    }

    template <>
    std::wstring
    acp_to_basic_string(std::string_view in, std::error_code& ec)
    {
        std::wstring out;
        multi_byte_to_utf16(multi_byte::cp_acp, in, out, ec);
        return out;
    }

    //
    // acp_to_* - char8_t flavor
    //

    template <>
    std::size_t
    acp_to_length(std::string_view in, char8_t const&)
    {
        return acp_to_char8_length(in);
    }

    template <>
    std::size_t
    acp_to_length(std::string_view in, char8_t const&, std::error_code& ec)
    {
        return acp_to_char8_length(in, ec);
    }

    template <>
    void
    acp_to_basic_string(std::string_view in, std::u8string& out)
    {
        std::u16string tempstring{};
        multi_byte_to_utf16(multi_byte::cp_acp, in, tempstring);
        utf::transcode(tempstring.begin(), tempstring.end(), out);
    }

    template <>
    void
    acp_to_basic_string(std::string_view in, std::u8string& out, std::error_code& ec)
    {
        std::u16string tempstring{};
        multi_byte_to_utf16(multi_byte::cp_acp, in, tempstring, ec);
        if (m::failed(ec))
            return;
        utf::transcode(std::u16string_view(tempstring.begin(), tempstring.end()), out, ec);
    }

    template <>
    std::u8string
    acp_to_basic_string(std::string_view in)
    {
        std::u16string tempstring{};
        multi_byte_to_utf16(multi_byte::cp_acp, in, tempstring);
        std::u8string out;
        utf::transcode(tempstring.begin(), tempstring.end(), out);
        return out;
    }

    template <>
    std::u8string
    acp_to_basic_string(std::string_view in, std::error_code& ec)
    {
        std::u16string tempstring{};
        multi_byte_to_utf16(multi_byte::cp_acp, in, tempstring, ec);
        if (m::failed(ec))
            return std::u8string{};
        std::u8string out;
        utf::transcode(std::u16string_view(tempstring.begin(), tempstring.end()), out, ec);
        return out;
    }

    //
    // acp_to_* - char16_t flavor
    //

    template <>
    std::size_t
    acp_to_length(std::string_view in, char16_t const&)
    {
        return acp_to_char16_length(in);
    }

    template <>
    std::size_t
    acp_to_length(std::string_view in, char16_t const&, std::error_code& ec)
    {
        return acp_to_char16_length(in, ec);
    }

    template <>
    void
    acp_to_basic_string(std::string_view in, std::u16string& out)
    {
        multi_byte_to_utf16(multi_byte::cp_acp, in, out);
    }

    template <>
    void
    acp_to_basic_string(std::string_view in, std::u16string& out, std::error_code& ec)
    {
        multi_byte_to_utf16(multi_byte::cp_acp, in, out, ec);
    }

    template <>
    std::u16string
    acp_to_basic_string(std::string_view in)
    {
        std::u16string out;
        multi_byte_to_utf16(multi_byte::cp_acp, in, out);
        return out;
    }

    template <>
    std::u16string
    acp_to_basic_string(std::string_view in, std::error_code& ec)
    {
        std::u16string out;
        multi_byte_to_utf16(multi_byte::cp_acp, in, out, ec);
        return out;
    }

    //
    // acp_to_* - char32_t flavor
    //

    template <>
    std::size_t
    acp_to_length(std::string_view in, char32_t const&)
    {
        return acp_to_char32_length(in);
    }

    template <>
    std::size_t
    acp_to_length(std::string_view in, char32_t const&, std::error_code& ec)
    {
        return acp_to_char32_length(in, ec);
    }

    template <>
    void
    acp_to_basic_string(std::string_view in, std::u32string& out)
    {
        std::u16string tempstring{};
        multi_byte_to_utf16(multi_byte::cp_acp, in, tempstring);
        utf::transcode(tempstring, out);
    }

    template <>
    void
    acp_to_basic_string(std::string_view in, std::u32string& out, std::error_code& ec)
    {
        std::u16string tempstring{};
        multi_byte_to_utf16(multi_byte::cp_acp, in, tempstring, ec);
        if (failed(ec))
            return;
        utf::transcode(tempstring, out, ec);
    }

    template <>
    std::u32string
    acp_to_basic_string(std::string_view in)
    {
        std::u16string tempstring{};
        multi_byte_to_utf16(multi_byte::cp_acp, in, tempstring);
        std::u32string out;
        utf::transcode(tempstring, out);
        return out;
    }

    template <>
    std::u32string
    acp_to_basic_string(std::string_view in, std::error_code& ec)
    {
        std::u16string tempstring{};
        multi_byte_to_utf16(multi_byte::cp_acp, in, tempstring, ec);
        if (failed(ec))
            return std::u32string{};
        std::u32string out;
        utf::transcode(tempstring, out, ec);
        return out;
    }

} // namespace m