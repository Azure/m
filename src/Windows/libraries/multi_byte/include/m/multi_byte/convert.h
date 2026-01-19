// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include <Windows.h>

#include <m/errors/errors.h>
#include <m/strings/convert.h>
#include <m/strings/tstring.h>
#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utility/concepts.h>
#include <m/utility/make_span.h>
#include <m/utility/type_traits.h>
#include <m/windows_strings/convert.h>

#include <m/multi_byte/code_page.h>
#include <m/multi_byte/multi_byte_to_utf16.h>
#include <m/multi_byte/to_span.h>
#include <m/multi_byte/utf16_to_multi_byte.h>

namespace m
{
    template <typename TCharIn, typename TCharOut>
        requires character<TCharIn> && character<TCharOut>
    void
    view_to_tstring(multi_byte::code_page           cp,
                    std::basic_string_view<TCharIn> in,
                    std::basic_string<TCharOut>&    out);

    template <typename TCharIn, typename TCharOut>
        requires character<TCharIn> && character<TCharOut>
    void
    view_to_tstring(multi_byte::code_page           cp,
                    std::basic_string_view<TCharIn> in,
                    std::basic_string<TCharOut>&    out,
                    std::error_code&                ec);

    template <typename TStringishIn, typename TCharOut>
        requires any_stringish<TStringishIn> && character<TCharOut>
    void
    to_tstring(multi_byte::code_page cp, TStringishIn&& in, std::basic_string<TCharOut>& out)
    {
        auto const view = to_basic_string_view_t<stringish_char_type_t<TStringishIn>>(
            std::forward<TStringishIn>(in));
        view_to_tstring(cp, view, out);
    }

    template <typename TStringishIn, typename TCharOut>
        requires any_stringish<TStringishIn> && character<TCharOut>
    void
    to_tstring(multi_byte::code_page        cp,
               TStringishIn&&               in,
               std::basic_string<TCharOut>& out,
               std::error_code&             ec)
    {
        auto const view = to_basic_string_view_t<stringish_char_type_t<TStringishIn>>(
            std::forward<TStringishIn>(in));
        view_to_tstring(cp, view, out, ec);
    }

    template <typename TCharOut, typename TStringishIn>
        requires any_stringish<TStringishIn> && character<TCharOut>
    std::basic_string<TCharOut>
    to_tstring(multi_byte::code_page cp, TStringishIn&& in)
    {
        auto const view = to_basic_string_view_t<stringish_char_type_t<TStringishIn>>(
            std::forward<TStringishIn>(in));
        std::basic_string<TCharOut> out;
        to_tstring(cp, view, out);
        return out;
    }

    template <typename TCharOut, typename TStringishIn>
        requires any_stringish<TStringishIn> && character<TCharOut>
    std::basic_string<TCharOut>
    to_tstring(multi_byte::code_page cp, TStringishIn&& in, std::error_code& ec)
    {
        auto const view = to_basic_string_view_t<stringish_char_type_t<TStringishIn>>(
            std::forward<TStringishIn>(in));
        std::basic_string<TCharOut> out;
        to_tstring(cp, view, out, ec);
        return out;
    }

    template <typename TCharOut, typename TStringishIn>
        requires any_stringish<TStringishIn> && character<TCharOut>
    std::optional<std::basic_string<TCharOut>>
    to_tstring(multi_byte::code_page cp, std::optional<TStringishIn> const& in)
    {
        if (!in.has_value())
            return std::nullopt;

        return to_tstring<TCharOut>(cp, in.value());
    }

    template <typename TCharOut, typename TStringishIn>
        requires any_stringish<TStringishIn> && character<TCharOut>
    std::optional<std::basic_string<TCharOut>>
    to_tstring(multi_byte::code_page cp, std::optional<TStringishIn> const& in, std::error_code& ec)
    {
        if (!in.has_value())
            return std::nullopt;

        return to_tstring<TCharOut>(cp, in.value(), ec);
    }

    template <typename TStringishIn, typename TCharOut>
        requires any_stringish<TStringishIn> && character<TCharOut>
    void
    to_tstring(multi_byte::code_page                       cp,
               std::optional<TStringishIn> const&          in,
               std::optional<std::basic_string<TCharOut>>& out)
    {
        if (!in.has_value())
        {
            out = std::nullopt;
            return;
        }

        out = std::nullopt;
        std::basic_string<TCharOut> return_value;
        to_tstring(cp, in.value(), return_value);
        out = return_value;
    }

    template <typename TStringishIn, typename TCharOut>
        requires any_stringish<TStringishIn> && character<TCharOut>
    void
    to_tstring(multi_byte::code_page                       cp,
               std::optional<TStringishIn> const&          in,
               std::optional<std::basic_string<TCharOut>>& out,
               std::error_code&                            ec)
    {
        if (!in.has_value())
        {
            out = std::nullopt;
            return;
        }

        out = std::nullopt;
        std::basic_string<TCharOut> return_value;
        to_tstring(cp, in.value(), return_value, ec);
        if (!ec)
            out = return_value;
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    void
    to_string(multi_byte::code_page cp, TStringishIn&& in, std::string& out)
    {
        to_tstring(cp, std::forward<TStringishIn>(in), out);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    void
    to_string(multi_byte::code_page cp, TStringishIn&& in, std::string& out, std::error_code& ec)
    {
        to_tstring(cp, std::forward<TStringishIn>(in), out, ec);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    std::string
    to_string(multi_byte::code_page cp, TStringishIn&& in)
    {
        return to_tstring<char>(cp, std::forward<TStringishIn>(in));
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    std::string
    to_string(multi_byte::code_page cp, TStringishIn&& in, std::error_code& ec)
    {
        return to_tstring<char>(cp, std::forward<TStringishIn>(in), ec);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    std::optional<std::string>
    to_string(multi_byte::code_page cp, std::optional<TStringishIn> const& in)
    {
        return to_tstring<char>(cp, in);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    std::optional<std::string>
    to_string(multi_byte::code_page cp, std::optional<TStringishIn> const& in, std::error_code& ec)
    {
        return to_tstring<char>(cp, in, ec);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    void
    to_string(multi_byte::code_page              cp,
              std::optional<TStringishIn> const& in,
              std::optional<std::string>&        out)
    {
        return to_tstring(cp, in, out);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    void
    to_string(multi_byte::code_page              cp,
              std::optional<TStringishIn> const& in,
              std::optional<std::string>&        out,
              std::error_code&                   ec)
    {
        return to_tstring(cp, in, out, ec);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    void
    to_wstring(multi_byte::code_page cp, TStringishIn&& in, std::wstring& out)
    {
        to_tstring(cp, std::forward<TStringishIn>(in), out);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    void
    to_wstring(multi_byte::code_page cp, TStringishIn&& in, std::wstring& out, std::error_code& ec)
    {
        to_tstring(cp, std::forward<TStringishIn>(in), out, ec);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    std::wstring
    to_wstring(multi_byte::code_page cp, TStringishIn&& in)
    {
        return to_tstring<wchar_t>(cp, std::forward<TStringishIn>(in));
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    std::wstring
    to_wstring(multi_byte::code_page cp, TStringishIn&& in, std::error_code& ec)
    {
        return to_tstring<wchar_t>(cp, std::forward<TStringishIn>(in), ec);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    std::optional<std::wstring>
    to_wstring(multi_byte::code_page cp, std::optional<TStringishIn> const& in)
    {
        return to_tstring<wchar_t>(cp, in);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    std::optional<std::wstring>
    to_wstring(multi_byte::code_page cp, std::optional<TStringishIn> const& in, std::error_code& ec)
    {
        return to_tstring<wchar_t>(cp, in, ec);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    void
    to_wstring(multi_byte::code_page              cp,
               std::optional<TStringishIn> const& in,
               std::optional<std::wstring>&       out)
    {
        return to_tstring(cp, in, out);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    void
    to_wstring(multi_byte::code_page              cp,
               std::optional<TStringishIn> const& in,
               std::optional<std::wstring>&       out,
               std::error_code&                   ec)
    {
        return to_tstring(cp, in, out, ec);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    void
    to_u8string(multi_byte::code_page cp, TStringishIn&& in, std::u8string& out)
    {
        to_tstring(cp, std::forward<TStringishIn>(in), out);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    void
    to_u8string(multi_byte::code_page cp,
                TStringishIn&&        in,
                std::u8string&        out,
                std::error_code&      ec)
    {
        to_tstring(cp, std::forward<TStringishIn>(in), out, ec);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    std::u8string
    to_u8string(multi_byte::code_page cp, TStringishIn&& in)
    {
        return to_tstring<char8_t>(cp, std::forward<TStringishIn>(in));
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    std::u8string
    to_u8string(multi_byte::code_page cp, TStringishIn&& in, std::error_code& ec)
    {
        return to_tstring<char8_t>(cp, std::forward<TStringishIn>(in), ec);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    std::optional<std::u8string>
    to_u8string(multi_byte::code_page cp, std::optional<TStringishIn> const& in)
    {
        return to_tstring<char8_t>(cp, in);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    std::optional<std::u8string>
    to_u8string(multi_byte::code_page              cp,
                std::optional<TStringishIn> const& in,
                std::error_code&                   ec)
    {
        return to_tstring<char8_t>(cp, in, ec);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    void
    to_u8string(multi_byte::code_page              cp,
                std::optional<TStringishIn> const& in,
                std::optional<std::u8string>&      out)
    {
        return to_tstring(cp, in, out);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    void
    to_u8string(multi_byte::code_page              cp,
                std::optional<TStringishIn> const& in,
                std::optional<std::u8string>&      out,
                std::error_code&                   ec)
    {
        return to_tstring(cp, in, out, ec);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    void
    to_u16string(multi_byte::code_page cp, TStringishIn&& in, std::u16string& out)
    {
        to_tstring(cp, std::forward<TStringishIn>(in), out);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    void
    to_u16string(multi_byte::code_page cp,
                 TStringishIn&&        in,
                 std::u16string&       out,
                 std::error_code&      ec)
    {
        to_tstring(cp, std::forward<TStringishIn>(in), out, ec);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    std::u16string
    to_u16string(multi_byte::code_page cp, TStringishIn&& in)
    {
        return to_tstring<char16_t>(cp, std::forward<TStringishIn>(in));
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    std::u16string
    to_u16string(multi_byte::code_page cp, TStringishIn&& in, std::error_code& ec)
    {
        return to_tstring<char16_t>(cp, std::forward<TStringishIn>(in), ec);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    std::optional<std::u16string>
    to_u16string(multi_byte::code_page cp, std::optional<TStringishIn> const& in)
    {
        return to_tstring<char16_t>(cp, in);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    std::optional<std::u16string>
    to_u16string(multi_byte::code_page              cp,
                 std::optional<TStringishIn> const& in,
                 std::error_code&                   ec)
    {
        return to_tstring<char16_t>(cp, in, ec);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    void
    to_u16string(multi_byte::code_page              cp,
                 std::optional<TStringishIn> const& in,
                 std::optional<std::u16string>&     out)
    {
        return to_tstring(cp, in, out);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    void
    to_u16string(multi_byte::code_page              cp,
                 std::optional<TStringishIn> const& in,
                 std::optional<std::u16string>&     out,
                 std::error_code&                   ec)
    {
        return to_tstring(cp, in, out, ec);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    void
    to_u32string(multi_byte::code_page cp, TStringishIn&& in, std::u32string& out)
    {
        to_tstring(cp, std::forward<TStringishIn>(in), out);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    void
    to_u32string(multi_byte::code_page cp,
                 TStringishIn&&        in,
                 std::u32string&       out,
                 std::error_code&      ec)
    {
        to_tstring(cp, std::forward<TStringishIn>(in), out, ec);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    std::u32string
    to_u32string(multi_byte::code_page cp, TStringishIn&& in)
    {
        return to_tstring<char32_t>(cp, std::forward<TStringishIn>(in));
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    std::u32string
    to_u32string(multi_byte::code_page cp, TStringishIn&& in, std::error_code& ec)
    {
        return to_tstring<char32_t>(cp, std::forward<TStringishIn>(in), ec);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    std::optional<std::u32string>
    to_u32string(multi_byte::code_page cp, std::optional<TStringishIn> const& in)
    {
        return to_tstring<char32_t>(cp, in);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    std::optional<std::u32string>
    to_u32string(multi_byte::code_page              cp,
                 std::optional<TStringishIn> const& in,
                 std::error_code&                   ec)
    {
        return to_tstring<char32_t>(cp, in, ec);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    void
    to_u32string(multi_byte::code_page              cp,
                 std::optional<TStringishIn> const& in,
                 std::optional<std::u32string>&     out)
    {
        return to_tstring(cp, in, out);
    }

    template <typename TStringishIn>
        requires any_stringish<TStringishIn>
    void
    to_u32string(multi_byte::code_page              cp,
                 std::optional<TStringishIn> const& in,
                 std::optional<std::u32string>&     out,
                 std::error_code&                   ec)
    {
        return to_tstring(cp, in, out, ec);
    }

} // namespace m
