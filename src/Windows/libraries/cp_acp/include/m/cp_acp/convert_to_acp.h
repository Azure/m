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
#include <m/utility/stringish.h>

#include <m/cp_acp/convert_utf16_to_acp.h>
#include <m/cp_acp/cp_acp.h>

namespace m
{
    //
    // Conversions to CP_ACP
    //
    // Unfortunately since they call ::MultiByteToWideChar and
    // / or ::WideCharToMultiByte, they cannot be constexpr.
    //

    //
    // to_acp_string
    //

    template <typename TStringish>
        requires any_stringish<TStringish>
    void
    to_acp_string(TStringish&& in, std::string& out)
    {
        using stringish_char_type = stringish_char_type_t<TStringish>;

        if constexpr (utf16_character<stringish_char_type>)
        {
            utf16_to_acp(to_basic_string_view_t<stringish_char_type>(std::forward<TStringish>(in)),
                         out);
        }
        else if constexpr (std::same_as<stringish_char_type, char>)
        {
            auto view = to_basic_string_view_t<stringish_char_type>(std::forward<TStringish>(in));
            out       = std::string(view);
        }
        else
        {
            auto temp = to_wstring(std::forward<TStringish>(in));
            utf16_to_acp(std::wstring_view(temp.begin(), temp.end()), out);
        }
    }

    template <typename TStringish>
        requires any_stringish<TStringish>
    std::string
    to_acp_string(TStringish&& in)
    {
        std::string out;
        to_acp_string(std::forward<TStringish>(in), out);
        return out;
    }

    template <typename TStringish>
        requires any_stringish<TStringish>
    void
    to_acp_string(TStringish&& in, std::string& out, std::error_code& ec)
    {
        using stringish_char_type = stringish_char_type_t<TStringish>;

        if constexpr (utf16_character<stringish_char_type>)
        {
            utf16_to_acp(
                to_basic_string_view_t<stringish_char_type>(std::forward<TStringish>(in)), out, ec);
        }
        else
        {
            auto temp = to_wstring(std::forward<TStringish>(in));
            utf16_to_acp(std::wstring_view(temp.begin(), temp.end()), out, ec);
        }
    }

    template <typename TStringish>
        requires any_stringish<TStringish>
    std::string
    to_acp_string(TStringish&& in, std::error_code& ec)
    {
        std::string out;
        to_acp_string(std::forward<TStringish>(in), out, ec);
        return out;
    }

    template <typename TStringish>
        requires any_stringish<TStringish>
    void
    to_acp(TStringish&& in, std::span<char>& out)
    {
        using stringish_char_type = stringish_char_type_t<TStringish>;

        if constexpr (utf16_character<stringish_char_type>)
        {
            utf16_to_acp(to_basic_string_view_t<stringish_char_type>(std::forward<TStringish>(in)),
                         out);
        }
        else
        {
            auto temp = to_wstring(std::forward<TStringish>(in));
            utf16_to_acp(std::wstring_view(temp.begin(), temp.end()), out);
        }
    }

    template <typename TStringish>
        requires any_stringish<TStringish>
    void
    to_acp(TStringish&& in, std::span<char>& out, std::error_code& ec)
    {
        using stringish_char_type = stringish_char_type_t<TStringish>;

        if constexpr (utf16_character<stringish_char_type>)
        {
            utf16_to_acp(
                to_basic_string_view_t<stringish_char_type>(std::forward<TStringish>(in)), out, ec);
        }
        else
        {
            auto temp = to_wstring(std::forward<TStringish>(in));
            utf16_to_acp(std::wstring_view(temp.begin(), temp.end()), out, ec);
        }
    }
} // namespace m
