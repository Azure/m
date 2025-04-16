// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <tuple>

#include <gsl/gsl>

#include "decode_result.h"
#include "exceptions.h"

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

        template <typename It>
        constexpr iter_decode_result<It>
        decode_utf8(It first, It last)
            requires std::input_iterator<It>
        {
            char32_t ch{};

            if (first == last)
                throw utf_sequence_truncated_error("read past end of iterator");
            std::byte const b1 = std::byte{*first++};
            if ((b1 & std::byte{0x80}) == std::byte{0x00})
            {
                ch = std::to_integer<char32_t>(b1);
            }
            else if ((b1 & std::byte{0xe0}) == std::byte{0xc0})
            {
                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");

                std::byte const b2 = std::byte{*first++};

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

                const std::byte b2 = std::byte{*first++};
                if ((b2 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");

                const std::byte b3 = std::byte{*first++};
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

                const std::byte b2 = std::byte{*first++};

                if ((b2 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");

                const std::byte b3 = std::byte{*first++};

                if ((b3 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");

                const std::byte b4 = std::byte{*first++};

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

                const std::byte b2 = std::byte{*first++};
                if ((b2 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");

                const std::byte b3 = std::byte{*first++};
                if ((b3 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");

                const std::byte b4 = std::byte{*first++};
                if ((b4 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");

                const std::byte b5 = std::byte{*first++};

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
                const std::byte b2 = std::byte{*first++};
                if ((b2 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");
                const std::byte b3 = std::byte{*first++};
                if ((b3 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");
                const std::byte b4 = std::byte{*first++};
                if ((b4 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");
                const std::byte b5 = std::byte{*first++};
                if ((b5 & std::byte{0xc0}) != std::byte{0x80})
                    throw utf_invalid_encoding_error("invalid Utf-8 encoding");

                if (first == last)
                    throw utf_sequence_truncated_error("read past end of iterator");
                const std::byte b6 = std::byte{*first++};
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

        constexpr decode_result
        decode_utf16le(std::span<std::byte const> input);

        constexpr decode_result
        decode_utf16be(std::span<std::byte const> input);

        constexpr decode_result
        decode_utf32(std::span<std::byte const> input);
    } // namespace utf
} // namespace m