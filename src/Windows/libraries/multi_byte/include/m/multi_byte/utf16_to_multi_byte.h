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

#include <m/multi_byte/code_page.h>

namespace m
{
    template <typename TCharIn>
        requires utf16_character<TCharIn>
    void
    utf16_to_multi_byte(multi_byte::code_page           cp,
                        std::basic_string_view<TCharIn> in,
                        std::basic_string<char>&        out);

    template <typename TCharIn>
        requires utf16_character<TCharIn>
    void
    utf16_to_multi_byte(multi_byte::code_page           cp,
                        std::basic_string_view<TCharIn> in,
                        std::basic_string<char>&        out,
                        std::error_code&                ec);

    template <typename TCharIn>
        requires utf16_character<TCharIn>
    void
    utf16_to_multi_byte(multi_byte::code_page           cp,
                        std::basic_string_view<TCharIn> in,
                        std::span<char>&                out);

    template <typename TCharIn>
        requires utf16_character<TCharIn>
    void
    utf16_to_multi_byte(multi_byte::code_page           cp,
                        std::basic_string_view<TCharIn> in,
                        std::span<char>&                out,
                        std::error_code&                ec);

    template <typename TCharIn>
        requires utf16_character<TCharIn>
    std::string
    utf16_to_multi_byte(multi_byte::code_page cp, std::basic_string_view<TCharIn> in)
    {
        std::string str;
        utf16_to_multi_byte(cp, in, str);
        return str;
    }

    template <typename TCharIn>
        requires utf16_character<TCharIn>
    std::string
    utf16_to_multi_byte(multi_byte::code_page           cp,
                        std::basic_string_view<TCharIn> in,
                        std::error_code&                ec)
    {
        std::string str;
        utf16_to_multi_byte(cp, in, str, ec);
        return str;
    }

    template <typename TCharIn>
        requires utf16_character<TCharIn>
    std::size_t
    utf16_to_multi_byte_length(multi_byte::code_page cp, std::basic_string_view<TCharIn> in);

    template <typename TCharIn>
        requires utf16_character<TCharIn>
    std::size_t
    utf16_to_multi_byte_length(multi_byte::code_page           cp,
                               std::basic_string_view<TCharIn> in,
                               std::error_code&                ec);

    template <typename TCharOut, typename OutIter, std::size_t BufferSize = 128>
        requires std::output_iterator<OutIter, char> && utf16_character<TCharOut>
    OutIter
    utf16_to_multi_byte(multi_byte::code_page            cp,
                        std::basic_string_view<TCharOut> in,
                        OutIter                          out_it,
                        std::error_code&                 ec)
    {
        std::array<char, BufferSize> buffer;
        auto                         input_cursor{in.data()};
        auto                         chars_left{in.size()};

        // Ensure that buffer has the space to encode at least one character.
        // Assume that 8 bytes is sufficient to hold any decoded UTF-16
        // character.
        M_INTERNAL_ERROR_CHECK(buffer.size() > 4);

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
                auto const view = std::basic_string_view<TCharOut>(input_cursor, chars_to_convert);
                auto       span = m::make_span(buffer);

                ec.clear();
                utf16_to_multi_byte(cp, view, span, ec);
                if (!ec)
                {
                    out_it = std::ranges::copy(span, out_it).out;

                    chars_left -= chars_to_convert;
                    input_cursor += chars_to_convert;

                    break;
                }

                if (ec.value() != HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))
                    return;

                chars_to_convert--;

                //
                // If you hit this, it means that trimming the input stream character by character
                // wasn't able to find something to fit into the output buffer. At the minimum,
                // one character in the input should be able to fit in the output.
                //
                M_INTERNAL_ERROR_CHECK(chars_to_convert > 0);
            }
        }

        return out_it;
    }

    template <typename TCharOut, typename OutIter, std::size_t BufferSize = 128>
        requires std::output_iterator<OutIter, char> && utf16_character<TCharOut>
    OutIter
    utf16_to_multi_byte(multi_byte::code_page            cp,
                        std::basic_string_view<TCharOut> in,
                        OutIter                          out_it)
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
                auto const view = std::basic_string_view<TCharOut>(input_cursor, chars_to_convert);
                auto       span = m::make_span(buffer);

                std::error_code ec;
                utf16_to_multi_byte(cp, view, span, ec);

                if (!ec)
                {
                    out_it = std::ranges::copy(span, out_it).out;

                    chars_left -= chars_to_convert;
                    input_cursor += chars_to_convert;

                    break;
                }

                if (ec.value() != HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))
                    m::throw_error(ec);

                chars_to_convert--;

                if (chars_to_convert == 0)
                    throw std::runtime_error("invalid UTF-16 character data");
            }
        }

        return out_it;
    }

} // namespace m
