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
#include <m/multi_byte/code_page.h>
#include <m/strings/convert.h>
#include <m/strings/tstring.h>
#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utility/concepts.h>
#include <m/utility/make_span.h>

#include <m/cp_acp/convert_acp_to_utf16.h>

namespace m
{
    std::size_t
    acp_to_char_length(std::string_view view);

    std::size_t
    acp_to_char_length(std::string_view view, std::error_code& ec);

    std::size_t
    acp_to_wchar_length(std::string_view view);

    std::size_t
    acp_to_wchar_length(std::string_view view, std::error_code& ec);

    std::size_t
    acp_to_char8_length(std::string_view view);

    std::size_t
    acp_to_char8_length(std::string_view view, std::error_code& ec);

    std::size_t
    acp_to_char16_length(std::string_view view);

    std::size_t
    acp_to_char16_length(std::string_view view, std::error_code& ec);

    std::size_t
    acp_to_char32_length(std::string_view view);

    std::size_t
    acp_to_char32_length(std::string_view view, std::error_code& ec);

    template <typename TChar>
    std::size_t
    acp_to_length(std::string_view in, TChar const& reference_character);

    template <typename TChar>
    std::size_t
    acp_to_length(std::string_view in, TChar const& reference_character, std::error_code& ec);

    template <typename TChar>
        requires character<TChar>
    void
    acp_to_span(std::string_view in, std::span<TChar>& out);

    template <typename TChar>
        requires character<TChar>
    void
    acp_to_span(std::string_view in, std::span<TChar>& out, std::error_code& ec);

    template <typename TCharOut>
        requires character<TCharOut>
    void
    acp_to_basic_string(std::string_view, std::basic_string<TCharOut>& out);

    template <typename TCharOut>
        requires character<TCharOut>
    void
    acp_to_basic_string(std::string_view, std::basic_string<TCharOut>& out, std::error_code& ec);

    template <typename TCharOut>
        requires character<TCharOut>
    std::basic_string<TCharOut> acp_to_basic_string(std::string_view);

    template <typename TCharOut>
        requires character<TCharOut>
    std::basic_string<TCharOut>
    acp_to_basic_string(std::string_view, std::error_code& ec);

    //
    //  acp_to_string
    //

    template <typename TStringish>
        requires stringish<TStringish, char>
    void
    acp_to_string(TStringish&& in, std::string& out)
    {
        acp_to_basic_string(to_basic_string_view_t<char>(std::forward<TStringish>(in)), out);
    }

    template <typename TStringish>
        requires stringish<TStringish, char>
    void
    acp_to_string(TStringish&& in, std::string& out, std::error_code& ec)
    {
        acp_to_basic_string(to_basic_string_view_t<char>(std::forward<TStringish>(in)), out, ec);
    }

    template <typename TStringish>
        requires stringish<TStringish, char>
    std::string
    acp_to_string(TStringish&& in)
    {
        return acp_to_basic_string<char>(
            to_basic_string_view_t<char>(std::forward<TStringish>(in)));
    }

    template <typename TStringish>
        requires stringish<TStringish, char>
    std::string
    acp_to_string(TStringish&& in, std::error_code& ec)
    {
        return acp_to_basic_string<char>(to_basic_string_view_t<char>(std::forward<TStringish>(in)),
                                         ec);
    }

    //
    // acp_to_wstring
    //
    template <typename TStringish>
        requires stringish<TStringish, char>
    void
    acp_to_wstring(TStringish&& in, std::wstring& out)
    {
        acp_to_basic_string(to_basic_string_view_t<char>(std::forward<TStringish>(in)), out);
    }

    template <typename TStringish>
        requires stringish<TStringish, char>
    void
    acp_to_wstring(TStringish&& in, std::wstring& out, std::error_code& ec)
    {
        acp_to_basic_string(to_basic_string_view_t<char>(std::forward<TStringish>(in)), out, ec);
    }

    template <typename TStringish>
        requires stringish<TStringish, char>
    std::wstring
    acp_to_wstring(TStringish&& in)
    {
        return acp_to_basic_string<wchar_t>(
            to_basic_string_view_t<char>(std::forward<TStringish>(in)));
    }

    template <typename TStringish>
        requires stringish<TStringish, char>
    std::wstring
    acp_to_wstring(TStringish&& in, std::error_code& ec)
    {
        return acp_to_basic_string<wchar_t>(
            to_basic_string_view_t<char>(std::forward<TStringish>(in)), ec);
    }

    //
    // acp_to_u8string
    //
    template <typename TStringish>
        requires stringish<TStringish, char>
    void
    acp_to_u8string(TStringish&& in, std::u8string& out)
    {
        acp_to_basic_string(to_basic_string_view_t<char>(std::forward<TStringish>(in)), out);
    }

    template <typename TStringish>
        requires stringish<TStringish, char>
    void
    acp_to_u8string(TStringish&& in, std::u8string& out, std::error_code& ec)
    {
        acp_to_basic_string(to_basic_string_view_t<char>(std::forward<TStringish>(in)), out, ec);
    }

    template <typename TStringish>
        requires stringish<TStringish, char>
    std::u8string
    acp_to_u8string(TStringish&& in)
    {
        return acp_to_basic_string<char8_t>(
            to_basic_string_view_t<char>(std::forward<TStringish>(in)));
    }

    template <typename TStringish>
        requires stringish<TStringish, char>
    std::u8string
    acp_to_u8string(TStringish&& in, std::error_code& ec)
    {
        return acp_to_basic_string<char8_t>(
            to_basic_string_view_t<char>(std::forward<TStringish>(in)), ec);
    }

    //
    // acp_to_u16string
    //
    template <typename TStringish>
        requires stringish<TStringish, char>
    void
    acp_to_u16string(TStringish&& in, std::u16string& out)
    {
        acp_to_basic_string(to_basic_string_view_t<char>(std::forward<TStringish>(in)), out);
    }

    template <typename TStringish>
        requires stringish<TStringish, char>
    void
    acp_to_u16string(TStringish&& in, std::u16string& out, std::error_code& ec)
    {
        acp_to_basic_string(to_basic_string_view_t<char>(std::forward<TStringish>(in)), out, ec);
    }

    template <typename TStringish>
        requires stringish<TStringish, char>
    std::u16string
    acp_to_u16string(TStringish&& in)
    {
        return acp_to_basic_string<char16_t>(
            to_basic_string_view_t<char>(std::forward<TStringish>(in)));
    }

    template <typename TStringish>
        requires stringish<TStringish, char>
    std::u16string
    acp_to_u16string(TStringish&& in, std::error_code& ec)
    {
        return acp_to_basic_string<char16_t>(
            to_basic_string_view_t<char>(std::forward<TStringish>(in)), ec);
    }

    //
    // acp_to_u32string
    //
    template <typename TStringish>
        requires stringish<TStringish, char>
    void
    acp_to_u32string(TStringish&& in, std::u32string& out)
    {
        acp_to_basic_string(to_basic_string_view_t<char>(std::forward<TStringish>(in)), out);
    }

    template <typename TStringish>
        requires stringish<TStringish, char>
    void
    acp_to_u32string(TStringish&& in, std::u32string& out, std::error_code& ec)
    {
        acp_to_basic_string(to_basic_string_view_t<char>(std::forward<TStringish>(in)), out, ec);
    }

    template <typename TStringish>
        requires stringish<TStringish, char>
    std::u32string
    acp_to_u32string(TStringish&& in)
    {
        return acp_to_basic_string<char32_t>(
            to_basic_string_view_t<char>(std::forward<TStringish>(in)));
    }

    template <typename TStringish>
        requires stringish<TStringish, char>
    std::u32string
    acp_to_u32string(TStringish&& in, std::error_code& ec)
    {
        return acp_to_basic_string<char32_t>(
            to_basic_string_view_t<char>(std::forward<TStringish>(in)), ec);
    }

} // namespace m
