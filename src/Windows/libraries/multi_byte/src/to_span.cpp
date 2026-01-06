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
    to_acp_span(std::wstring_view in, std::span<char>& spn)
    {
        to_span(m::multi_byte::cp_acp, in, spn);
    }

    void
    to_acp_span(wchar_t const* in, std::span<char>& spn)
    {
        to_span(m::multi_byte::cp_acp, in, spn);
    }

    void
    to_acp_span(std::u8string_view in, std::span<char>& spn)
    {
        to_span(m::multi_byte::cp_acp, in, spn);
    }

    void
    to_acp_span(char8_t const* in, std::span<char>& spn)
    {
        to_span(m::multi_byte::cp_acp, in, spn);
    }

    void
    to_acp_span(std::u16string_view in, std::span<char>& spn)
    {
        to_span(m::multi_byte::cp_acp, in, spn);
    }

    void
    to_acp_span(char16_t const* in, std::span<char>& spn)
    {
        to_span(m::multi_byte::cp_acp, in, spn);
    }

    void
    to_acp_span(std::u32string_view in, std::span<char>& spn)
    {
        to_span(m::multi_byte::cp_acp, in, spn);
    }

    void
    to_acp_span(char32_t const* in, std::span<char>& spn)
    {
        to_span(m::multi_byte::cp_acp, in, spn);
    }
} // namespace m

namespace m::multi_byte::details
{
    template <>
    void
    to_span(code_page, std::u8string_view, std::span<char>&)
    {
        M_NOT_IMPLEMENTED("sorry not implemented");
    }

    template <>
    void
    to_span(code_page cp, std::wstring_view in, std::span<char>& out)
    {
        utf16_to_multi_byte(cp, in, out);
    }

    template <>
    void
    to_span(code_page cp, std::string_view in, std::span<wchar_t>& out)
    {
        multi_byte_to_utf16(cp, in, out);
    }

    template <>
    void
    to_span(code_page, std::u32string_view, std::span<char>&)
    {
        M_NOT_IMPLEMENTED("UTF-32 to CP_ACP conversion not implemented");
        // m::multi_byte::details::utf32_to_multi_byte(cp, view, spn);
    }

    template <>
    void
    to_span(code_page cp, std::string_view in, std::span<char16_t>& out)
    {
        auto spn = std::span<wchar_t>(reinterpret_cast<wchar_t*>(out.data()), out.size());
        to_span(cp, in, spn);
        out = std::span<char16_t>(reinterpret_cast<char16_t*>(spn.data()), spn.size());
    }

    template <>
    void
    to_span(code_page cp, std::u16string_view in, std::span<char>& out)
    {
        to_span(cp, std::wstring_view(reinterpret_cast<wchar_t const*>(in.data()), in.size()), out);
    }

    template <>
    void
    to_span(code_page cp, std::u16string_view in, std::span<char>& out, std::error_code& ec)
    {
        to_span(
            cp, std::wstring_view(reinterpret_cast<wchar_t const*>(in.data()), in.size()), out, ec);
    }

#if 0
    template <>
    void
    to_span(code_page cp, std::wstring_view in, std::span<char>& out, std::error_code& ec)
    {
        utf16_to_multi_byte(cp, in, out, ec);
    }
#endif

} // namespace m::multi_byte::details
