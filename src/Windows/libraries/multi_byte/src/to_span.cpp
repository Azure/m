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
    template <>
    void
    view_to_span(multi_byte::code_page, std::u8string_view, std::span<char>&)
    {
        M_NOT_IMPLEMENTED("sorry not implemented");
    }

    template <>
    void
    view_to_span(multi_byte::code_page cp, std::wstring_view in, std::span<char>& out)
    {
        utf16_to_multi_byte(cp, in, out);
    }

    template <>
    void
    view_to_span(multi_byte::code_page cp, std::string_view in, std::span<wchar_t>& out)
    {
        multi_byte_to_utf16(cp, in, out);
    }

    template <>
    void
    view_to_span(multi_byte::code_page, std::u32string_view, std::span<char>&)
    {
        M_NOT_IMPLEMENTED("UTF-32 to CP_ACP conversion not implemented");
        // m::multi_byte::details::utf32_to_multi_byte(cp, view, spn);
    }

    template <>
    void
    view_to_span(multi_byte::code_page cp, std::string_view in, std::span<char16_t>& out)
    {
        auto spn = std::span<wchar_t>(reinterpret_cast<wchar_t*>(out.data()), out.size());
        view_to_span(cp, in, spn);
        out = std::span<char16_t>(reinterpret_cast<char16_t*>(spn.data()), spn.size());
    }

    template <>
    void
    view_to_span(multi_byte::code_page cp, std::u16string_view in, std::span<char>& out)
    {
        view_to_span(cp, std::wstring_view(reinterpret_cast<wchar_t const*>(in.data()), in.size()), out);
    }

    template <>
    void
    view_to_span(multi_byte::code_page cp,
                 std::u16string_view   in,
                 std::span<char>&      out,
                 std::error_code&      ec)
    {
        view_to_span(
            cp, std::wstring_view(reinterpret_cast<wchar_t const*>(in.data()), in.size()), out, ec);
    }

} // namespace m
