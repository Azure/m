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
    std::size_t
    multi_byte_to_utf16_length(multi_byte::code_page cp, std::string_view in);

    std::size_t
    multi_byte_to_utf16_length(multi_byte::code_page cp, std::string_view in, std::error_code& ec);

    template <typename TCharOut>
        requires utf16_character<TCharOut>
    void
    multi_byte_to_utf16(multi_byte::code_page cp, std::string_view in, std::span<TCharOut>& out);

    template <typename TCharOut>
        requires utf16_character<TCharOut>
    void
    multi_byte_to_utf16(multi_byte::code_page        cp,
                        std::string_view             in,
                        std::basic_string<TCharOut>& out);

    template <typename TCharOut>
        requires utf16_character<TCharOut>
    std::basic_string<TCharOut>
    multi_byte_to_utf16(multi_byte::code_page cp, std::string_view in);

    template <typename TCharOut>
        requires utf16_character<TCharOut>
    void
    multi_byte_to_utf16(multi_byte::code_page        cp,
                        std::string_view             in,
                        std::basic_string<TCharOut>& out,
                        std::error_code&             ec);

    template <typename TCharOut>
        requires utf16_character<TCharOut>
    std::basic_string<TCharOut>
    multi_byte_to_utf16(multi_byte::code_page cp, std::string_view in, std::error_code& ec);

    template <typename TCharOut>
        requires utf16_character<TCharOut>
    void
    multi_byte_to_utf16(multi_byte::code_page cp,
                        std::string_view      view,
                        std::span<TCharOut>&  buffer,
                        std::error_code&      ec);

    template <typename OutIter, typename Utf16CharT = wchar_t, std::size_t BufferSize = 128>
        requires std::output_iterator<OutIter, Utf16CharT> && utf16_character<Utf16CharT>
    OutIter
    multi_byte_to_utf16(multi_byte::code_page cp, std::string_view in, OutIter it)
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
                auto       span = make_span(buffer);

                std::error_code ec;
                multi_byte_to_utf16(cp, view, span, ec);
                if (!failed(ec))
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
} // namespace m
