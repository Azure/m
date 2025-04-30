// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <numeric>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/errors/errors.h>
#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utility/make_span.h>

#include <Windows.h>

#include "code_page.h"

namespace m
{
    namespace multi_byte
    {
        namespace details
        {
            std::size_t
            utf16_to_multi_byte(code_page cp, std::wstring_view view, std::span<char>& buffer);

            std::size_t
            utf16_to_multi_byte(code_page cp, std::u16string_view view, std::span<char>& buffer);

            windows::win32_error_code
            try_utf16_to_multi_byte(code_page cp, std::wstring_view view, std::span<char>& buffer);

            windows::win32_error_code
            try_utf16_to_multi_byte(code_page           cp,
                                    std::u16string_view view,
                                    std::span<char>&    buffer);

            std::string
            utf16_to_multi_byte_fn(code_page cp, std::wstring_view view);

            std::string
            utf16_to_multi_byte_fn(code_page cp, std::u16string_view view);

            std::size_t
            utf16_to_acp(std::wstring_view view, std::span<char>& buffer);

            std::size_t
            utf16_to_acp(std::u16string_view view, std::span<char>& buffer);

        } // namespace details

        std::size_t
        multi_byte_to_utf16_length(code_page cp, std::string_view view);

        std::size_t
        multi_byte_to_utf16(code_page cp, std::string_view view, std::span<wchar_t>& buffer);

        std::size_t
        multi_byte_to_utf16(code_page cp, std::string_view view, std::span<char16_t>& buffer);

        void
        multi_byte_to_utf16(code_page cp, std::string_view view, std::wstring& string);

        void
        multi_byte_to_utf16(code_page cp, std::string_view view, std::u16string& string);

        windows::win32_error_code
        try_multi_byte_to_utf16(code_page cp, std::string_view view, std::span<wchar_t>& buffer);

        windows::win32_error_code
        try_multi_byte_to_utf16(code_page cp, std::string_view view, std::span<char16_t>& buffer);

        template <typename OutIter, typename Utf16CharT = wchar_t, std::size_t BufferSize = 128>
            requires std::output_iterator<OutIter, Utf16CharT> &&
                     (std::is_same_v<Utf16CharT, wchar_t> || std::is_same_v<Utf16CharT, char16_t>)
        OutIter multi_byte_to_utf16(code_page cp, std::string_view in, OutIter it)
        {
            std::array<Utf16CharT, BufferSize> buffer;
            auto                               input_cursor{in.data()};
            auto                               chars_left{in.size()};

            while (chars_left != 0)
            {
                std::size_t chars_to_convert{(std::min)(chars_left, buffer.size())};

                // mbcs -> Utf16 cannot (proof separate, as long as chars
                // above U+FFFF aren't encoded in single byte form in the
                // source) expand in terms of count, so we assume that
                // failures are either fundamentally bad encodings or
                // that we ran out of output buffer. We will assume out of
                // output buffer and trim down conversion length until
                // we get a successful conversion or zero length.
                //
                // If we hit zero length, we will assume it was a bad
                // encoding, because it must have been. Otherwise we have
                // to make the interface with try_acp_to_utf16()
                // significantly more complicated.
                //

                for (;;)
                {
                    auto const view = std::string_view(input_cursor, chars_to_convert);
                    auto       span = m::make_span(buffer);

                    if (windows::is_success(try_multi_byte_to_utf16(cp, view, span)))
                    {
                        it = std::ranges::copy(span, it).out;

                        chars_left -= chars_to_convert;
                        input_cursor += chars_to_convert;

                        break;
                    }

                    chars_to_convert--;

                    if (chars_to_convert == 0)
                        throw std::runtime_error("invalid multi_byte character data");
                }
            }

            return it;
        }

        std::size_t
        acp_to_utf16_length(std::string_view view);

        std::size_t
        acp_to_utf16(std::string_view view, std::span<wchar_t>& buffer);

        std::size_t
        acp_to_utf16(std::string_view view, std::span<char16_t>& buffer);

        template <typename InputIt,
                  typename Utf16CharT  = wchar_t,
                  typename CharTraitsT = std::char_traits<Utf16CharT>>
            requires std::input_iterator<InputIt> && std::forward_iterator<InputIt> &&
                     std::contiguous_iterator<InputIt> &&
                     std::is_same_v<std::iter_value_t<InputIt>, char>
        void
        acp_to_utf16(InputIt front, InputIt end, std::basic_string<Utf16CharT, CharTraitsT>& out)
        {
            auto const view          = std::string_view(front, end);
            auto const wchars_needed = acp_to_utf16_length(view);

            out.resize_and_overwrite(wchars_needed,
                                     [view](auto buffer, auto buffer_size) -> std::size_t {
                                         auto span = m::make_span(buffer, buffer_size);
                                         return acp_to_utf16(view, span);
                                     });
        }

        template <typename TInput,
                  typename Utf16CharT  = wchar_t,
                  typename CharTraitsT = std::char_traits<Utf16CharT>>
        void
        acp_to_utf16(TInput input, std::basic_string<Utf16CharT, CharTraitsT>& out)
        {
            acp_to_utf16(std::begin(input), std::end(input), out);
        }

        template <typename OutIter, typename Utf16CharT, std::size_t BufferSize = 128>
            requires std::output_iterator<OutIter, Utf16CharT> &&
                     (std::is_same_v<Utf16CharT, wchar_t> || std::is_same_v<Utf16CharT, char16_t>)
        OutIter acp_to_utf16(std::string_view in, OutIter it)
        {
            return multibyte_to_utf16(cp_acp, in, it);
        }

        //
        //
        //

        std::size_t
        utf16_to_multi_byte_length(code_page cp, std::wstring_view view);

        std::size_t
        utf16_to_multi_byte_length(code_page cp, std::u16string_view view);

        template <typename Utf16CharT  = wchar_t,
                  typename CharTraitsT = std::char_traits<Utf16CharT>>
            requires std::is_same_v<Utf16CharT, wchar_t> || std::is_same_v<Utf16CharT, char16_t>
        std::size_t
        utf16_to_multi_byte(code_page                                       cp,
                            std::basic_string_view<Utf16CharT, CharTraitsT> view,
                            std::span<char>&                                buffer)
        {
            return details::utf16_to_multi_byte(cp, view, buffer);
        }

        template <typename Utf16CharT  = wchar_t,
                  typename CharTraitsT = std::char_traits<Utf16CharT>>
        windows::win32_error_code
        try_utf16_to_multi_byte(code_page                                       cp,
                                std::basic_string_view<Utf16CharT, CharTraitsT> view,
                                std::span<char>&                                buffer)
            requires std::is_same_v<Utf16CharT, wchar_t> || std::is_same_v<Utf16CharT, char16_t>
        {
            return details::try_utf16_to_multi_byte(cp, view, buffer);
        }

        template <typename OutIter,
                  typename Utf16CharT    = wchar_t,
                  std::size_t BufferSize = 128,
                  typename CharTraitsT   = std::char_traits<Utf16CharT>>
            requires std::output_iterator<OutIter, char> &&
                     (std::is_same_v<Utf16CharT, wchar_t> || std::is_same_v<Utf16CharT, char16_t>)
        OutIter utf16_to_multi_byte(code_page                                              cp,
                                    std::basic_string_view<Utf16CharT, CharTraitsT> const& in,
                                    OutIter                                                it)
        {
            std::array<char, BufferSize> buffer;
            auto                         input_cursor{in.data()};
            auto                         chars_left{in.size()};

            while (chars_left != 0)
            {
                std::size_t chars_to_convert{(std::min)(chars_left, buffer.size())};

                // mbcs -> Utf16 cannot (proof separate, as long as chars
                // above U+FFFF aren't encoded in single byte form in the
                // source) expand in terms of count, so we assume that
                // failures are either fundamentally bad encodings or
                // that we ran out of output buffer. We will assume out of
                // output buffer and trim down conversion length until
                // we get a successful conversion or zero length.
                //
                // If we hit zero length, we will assume it was a bad
                // encoding, because it must have been. Otherwise we have
                // to make the interface with try_acp_to_utf16()
                // significantly more complicated.
                //

                for (;;)
                {
                    // This should probably be converted to use std::size_t
                    // indices and .substr() or such that way the type
                    // wouldn't have to be named here.
                    auto const view = std::basic_string_view<Utf16CharT, CharTraitsT>(
                        input_cursor, chars_to_convert);
                    auto span = m::make_span(buffer);

                    if (windows::is_success(try_utf16_to_multi_byte(cp, view, span)))
                    {
                        it = std::ranges::copy(span, it).out;

                        chars_left -= chars_to_convert;
                        input_cursor += chars_to_convert;

                        break;
                    }

                    chars_to_convert--;

                    if (chars_to_convert == 0)
                        throw std::runtime_error("invalid UTF-16 character data");
                }
            }

            return it;
        }

        template <typename Utf16CharT, typename CharTraitsT = std::char_traits<Utf16CharT>>
        std::string
        utf16_to_multi_byte(code_page cp, std::basic_string_view<Utf16CharT, CharTraitsT> view)
        {
            return details::utf16_to_multi_byte_fn(cp, view);
        }

        void
        utf16_to_multi_byte(code_page cp, std::wstring_view view, std::string& string);

        void
        utf16_to_multi_byte(code_page cp, std::u16string_view view, std::string& string);

        std::size_t
        utf16_to_acp_length(std::wstring_view view);

        std::size_t
        utf16_to_acp_length(std::u16string_view view);

        template <typename Utf16CharT, typename CharTraitsT = std::char_traits<Utf16CharT>>
            requires std::is_same_v<Utf16CharT, wchar_t> || std::is_same_v<Utf16CharT, char16_t>
        std::size_t
        utf16_to_acp(std::basic_string_view<Utf16CharT, CharTraitsT> view, std::span<char>& buffer)
        {
            return details::utf16_to_acp(view, buffer);
        }

        template <typename InputIt,
                  typename Utf16CharT  = wchar_t,
                  typename CharTraitsT = std::char_traits<Utf16CharT>>
            requires std::input_iterator<InputIt> && std::forward_iterator<InputIt> &&
                     std::contiguous_iterator<InputIt> &&
                     (std::is_same_v<Utf16CharT, wchar_t> || std::is_same_v<Utf16CharT, char16_t>)
        void utf16_to_acp(InputIt front, InputIt end, std::string& out)
        {
            auto const view         = std::basic_string_view<Utf16CharT, CharTraitsT>(front, end);
            auto const chars_needed = utf16_to_acp_length(view);

            out.resize_and_overwrite(chars_needed,
                                     [view](auto buffer, auto buffer_size) -> std::size_t {
                                         auto span = m::make_span(buffer, buffer_size);
                                         return utf16_to_acp(view, span);
                                     });
        }

        template <typename TInput>
        void
        utf16_to_acp(TInput input, std::string& out)
        {
            out.clear();
            acp_to_utf16(std::begin(input), std::end(input), out);
        }

        void
        utf16_to_acp(std::wstring_view view, std::string& string);

        void
        utf16_to_acp(std::u16string_view view, std::string& string);

        template <typename OutIter,
                  typename Utf16CharT    = wchar_t,
                  std::size_t BufferSize = 128,
                  typename CharTraitsT   = std::char_traits<Utf16CharT>>
        OutIter
        utf16_to_acp(std::basic_string_view<Utf16CharT, CharTraitsT> const& view, OutIter out_it)
            requires std::output_iterator<OutIter, char> &&
                     (std::is_same_v<Utf16CharT, wchar_t> || std::is_same_v<Utf16CharT, char16_t>)
        {
            return utf16_to_multi_byte(cp_acp, view, out_it);
        }

        template <typename Utf16CharT  = wchar_t,
                  typename CharTraitsT = std::char_traits<Utf16CharT>>
        std::string
        utf16_to_acp(std::basic_string_view<Utf16CharT, CharTraitsT> view)
        {
            return utf16_to_multi_byte_fn(cp_acp, view);
        }
    } // namespace multi_byte

    std::string
    to_string(multi_byte::code_page cp, std::wstring_view view);

    void
    to_string(multi_byte::code_page cp, std::wstring_view view, std::string& str);

    std::string
    to_string(multi_byte::code_page cp, std::u8string_view view);

    void
    to_string(multi_byte::code_page cp, std::u8string_view view, std::string& str);

    std::string
    to_string(multi_byte::code_page cp, std::u16string_view view);

    void
    to_string(multi_byte::code_page cp, std::u16string_view view, std::string& str);

    std::string
    to_string(multi_byte::code_page cp, std::u32string_view view);

    void
    to_string(multi_byte::code_page cp, std::u32string_view view, std::string& str);

    //
    std::wstring
    to_wstring(multi_byte::code_page cp, std::string_view view);

    void
    to_wstring(multi_byte::code_page cp, std::string_view view, std::wstring& str);

    //
    std::u8string
    to_u8string(multi_byte::code_page cp, std::string_view view);

    void
    to_u8string(multi_byte::code_page cp, std::string_view view, std::u8string& str);

    //
    std::u16string
    to_u16string(multi_byte::code_page cp, std::string_view view);

    void
    to_u16string(multi_byte::code_page cp, std::string_view view, std::u16string& str);

    //
    std::u32string
    to_u32string(multi_byte::code_page cp, std::string_view view);

    void
    to_u32string(multi_byte::code_page cp, std::string_view view, std::u32string& str);

    //
    // Conversions to and from CP_ACP
    //
    // Unfortunately since they call ::MultiByteToWideChar and
    // / or ::WideCharToMultiByte, they cannot be constexpr.
    //

    //
    // to_acp_string
    //
    std::string
    to_acp_string(std::wstring_view view);

    void
    to_acp_string(std::wstring_view view, std::string& str);

    std::string
    to_acp_string(std::u8string_view view);

    void
    to_acp_string(std::u8string_view view, std::string& str);

    std::string
    to_acp_string(std::u16string_view view);

    void
    to_acp_string(std::u16string_view view, std::string& str);

    std::string
    to_acp_string(std::u32string_view view);

    void
    to_acp_string(std::u32string_view view, std::string& str);

    //
    // acp_to_wstring
    //
    std::wstring
    acp_to_wstring(std::string_view view);

    void
    acp_to_wstring(std::string_view view, std::wstring& str);

    //
    // acp_to_u8string
    //
    std::u8string
    acp_to_u8string(std::string_view view);

    void
    acp_to_u8string(std::string_view view, std::u8string& str);

    //
    // to_u8string
    //
    constexpr std::u8string
    to_u8string(std::wstring_view view);

    constexpr void
    to_u8string(std::wstring_view view, std::u8string& str);

    //
    // acp_to_u16string
    //
    std::u16string
    acp_to_u16string(std::string_view view);

    void
    acp_to_u16string(std::string_view view, std::u16string& str);

    //
    // to_u16string
    //
    constexpr std::u16string
    to_u16string(std::wstring_view view);

    constexpr void
    to_u16string(std::wstring_view view, std::u16string& str);

    //
    // acp_to_u32string
    //
    std::u32string
    acp_to_u32string(std::string_view view);

    void
    acp_to_u32string(std::string_view view, std::u32string& str);
} // namespace m
