// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <stdexcept>
#include <tuple>

#include "decode_result.h"
#include "exceptions.h"

//
// For UTF-8 encoding and decoding, char, std::byte, and char8_t have
//  similar treatment in certain circumstances, so generally most of the
//  encoding and decoding is present in the headers so that it can be
//  constexpr and common between the three types.
// 
//  You cannot cast between unrelated types and remain constexpr so
//  even though you "know" that a std::byte* and a char* are "really" the
//  same thing, it's not possible to take advantage of this in a constexpr
//  implementation. If there were some massive speedup to a non-constexpr
//  implementation like there sometimes are to strlen etc then go ahead and
//  make non-constexpr implementations and use the appropriate mechanisms
//  to select.
// 
// Utf-16 has a similar challenge but on two axis. First, sometimes the
//  iterator is over a collection where sizeof(T) == 2 (e.g. char16_t) and
//  others it will be over a byte array. In order to be constexpr here,
//  there have to be two separate implementations, one can be common to
//  the sizeof(*iter)==2, and the other for when it is ==1. Perhaps this
//  too can be unified but it's not how the code has been written at this
//  point.
// 
// The sizing functions got confused between whether they were reporting
//  their sizes in bytes or quanta so the names changed to _count and
//  _bytes. Since UTF-32 does not have a variable length output, there
//  is no _count variant; feel free to add one in the future if there is
//  some cause. It will return 1 in all cases.
// 
// When building for Windows, wchar_t is a UTF-16LE type. It may be the
// language native wchar_t type or it may be "unsigned short" for some
// legacy Windows code bases. The char type represents a character in an
// unspecified encoding. The topic is far too involved to try to describe
// right here but generally speaking, chances are that it is in the so-
// called "ANSI Code Page", CP_ACP, and requires Windows specific APIs to
// interpret correctly. Use your favorite search engine to search for
// "MultiByteToWideChar CP_ACP" to find out more than you probably want
// to know.
// 
// When building for Linux, wchar_t is probably a 32-bit type, perhaps
// the same as char32_t but perhaps not. Common wisdom is that it's
// UTF-32, although this does seem to depend on the C runtime library and
// compiler, and its settings, that you are using. This is the default
// assumption that we are making here.
// 
// Further, regardless of building for Linux or not, many open source
// software projects make the assumption that "char" means UTF-8, which
// can play havoc on Windows.
// 
// This library focuses on encoding and decoding, see the m::strings library
// for more information about techniques for working with open source
// software that uses char/std::string/std::string_view for UTF-8
// based strings. There is no magic bullet but we try to provide some
// tools to manage the complexity.
//

namespace m
{
    namespace utf
    {
        constexpr decode_result
        decode_utf8(std::span<std::byte const> input);

        template <typename It>
        struct iter_decode_result
        {
            It       it;
            char32_t ch;
        };

        namespace details
        {
            constexpr std::byte
            to_byte(std::byte b)
            {
                return b;
            }

            constexpr std::byte
            to_byte(char8_t ch)
            {
                return std::byte{ch};
            }

            constexpr char16_t
            to_utf16le(char16_t ch)
            {
                return ch;
            }
        } // namespace details

        template <typename It>
            requires std::input_iterator<It>
        constexpr iter_decode_result<It>
        decode_utf8(It first, It last)
        {
            char32_t ch{};

            if (first == last)
                throw utf_sequence_truncated_error("read past end of iterator");
            auto const b1 = details::to_byte(*first++);
            if ((b1 & std::byte{0x80}) == std::byte{0x00})
            {
                ch = std::to_integer<char32_t>(b1);
            }
            else if ((b1 & std::byte{0xe0}) == std::byte{0xc0})
            {
                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");

                auto const b2 = details::to_byte(*first++);

                if ((b2 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                ch = std::to_integer<char32_t>(b1 & std::byte{0x1f}) << 6 |
                     std::to_integer<char32_t>(b2 & std::byte{0x3f});

                // Check for non-shortest-length encoding
                if (ch < 0x00000080)
                    throw utf_invalid_encoding_error("Non-shortest Utf-8 encoding");
            }
            else if ((b1 & std::byte{0xf0}) == std::byte{0xe0})
            {
                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");

                auto const b2 = details::to_byte(*first++);
                if ((b2 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");

                auto const b3 = details::to_byte(*first++);
                if ((b3 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                ch = (((std::to_integer<char32_t>(b1 & std::byte{0x0f}) << 6) |
                       std::to_integer<char32_t>(b2 & std::byte{0x3f}))
                      << 6) |
                     std::to_integer<char32_t>(b3 & std::byte{0x3f});

                // Check for non-shortest-length encoding
                if (ch < 0x00000800)
                    throw utf_invalid_encoding_error("Non-shortest Utf-8 encoding");
            }
            else if ((b1 & std::byte{0xf8}) == std::byte{0xf0})
            {
                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");

                auto const b2 = details::to_byte(*first++);

                if ((b2 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");

                auto const b3 = details::to_byte(*first++);

                if ((b3 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");

                auto const b4 = details::to_byte(*first++);

                if ((b4 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                ch = (((((std::to_integer<char32_t>(b1 & std::byte{0x07}) << 6) |
                         std::to_integer<char32_t>(b2 & std::byte{0x3f}))
                        << 6) |
                       std::to_integer<char32_t>(b3 & std::byte{0x3f}))
                      << 6) |
                     std::to_integer<char32_t>(b4 & std::byte{0x3f});

                if (ch < 0x00010000)
                    throw utf_invalid_encoding_error("Non-shortest Utf-8 encoding");
            }
            else if ((b1 & std::byte{0xfc}) == std::byte{0xf8})
            {
                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");

                auto const b2 = details::to_byte(*first++);
                if ((b2 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");

                auto const b3 = details::to_byte(*first++);
                if ((b3 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");

                auto const b4 = details::to_byte(*first++);
                if ((b4 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");

                auto const b5 = details::to_byte(*first++);

                if ((b5 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                ch = (((((((std::to_integer<char32_t>(b1 & std::byte{0x03}) << 6) |
                           std::to_integer<char32_t>(b2 & std::byte{0x3f}))
                          << 6) |
                         std::to_integer<char32_t>(b3 & std::byte{0x3f}))
                        << 6) |
                       std::to_integer<char32_t>(b4 & std::byte{0x3f}))
                      << 6) |
                     std::to_integer<char32_t>(b5 & std::byte{0x3f});

                if (ch < 0x00200000)
                    throw utf_invalid_encoding_error("non-shortest Utf-8 encoding");
            }
            else if ((b1 & std::byte{0xfe}) == std::byte{0xfc})
            {
                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");
                auto const b2 = details::to_byte(*first++);
                if ((b2 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");
                auto const b3 = details::to_byte(*first++);
                if ((b3 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");
                auto const b4 = details::to_byte(*first++);
                if ((b4 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");
                auto const b5 = details::to_byte(*first++);
                if ((b5 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");
                auto const b6 = details::to_byte(*first++);
                if ((b6 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                ch = (((((((((std::to_integer<char32_t>(b1 & std::byte{0x01}) << 6) |
                             std::to_integer<char32_t>(b2 & std::byte{0x3f}))
                            << 6) |
                           std::to_integer<char32_t>(b3 & std::byte{0x3f}))
                          << 6) |
                         std::to_integer<char32_t>(b4 & std::byte{0x3f}))
                        << 6) |
                       std::to_integer<char32_t>(b5 & std::byte{0x3f}))
                      << 6) |
                     std::to_integer<char32_t>(b6 & std::byte{0x3f});

                if (ch < 0x04000000)
                    throw utf_invalid_encoding_error("non-shortest Utf-8 encoding");
            }
            else
                throw utf_invalid_encoding_error("invalid Utf-8 encoding");

            return iter_decode_result<It>{.it = first, .ch = ch};
        }

        template <typename It>
            requires std::input_iterator<It>
        constexpr iter_decode_result<It>
        decode_utf16(It first, It last)
        {
            char32_t ch{};

            if (first == last)
                throw std::runtime_error("read past end of iterator");

            auto const ch1 = details::to_utf16le(*first++);
            if (ch1 < 0xd800)
            {
                ch = ch1;
            }
            else if (ch1 <= 0xdbff)
            {
                if (first == last)
                    throw std::runtime_error("utf-16 sequence truncated");

                auto const ch2 = details::to_utf16le(*first++);

                if ((ch2 < 0xdc00) || (ch2 > 0xdfff))
                    throw std::runtime_error("utf-16 invalid surrogate pair");

                ch = ((((ch1 - 0xd800) * 1024) + (ch2 - 0xdc00)) + 0x10000);
            }
            else if (ch1 <= 0xdfff)
                throw std::runtime_error("invalid UTF-16 encoding");
            else
                ch = ch1;

            return iter_decode_result<It>{.it = first, .ch = ch};
        }

        constexpr decode_result
        decode_utf16le(std::span<std::byte const> input);

        constexpr decode_result
        decode_utf16be(std::span<std::byte const> input);

        constexpr decode_result
        decode_utf32(std::span<std::byte const> input);
    } // namespace utf
} // namespace m