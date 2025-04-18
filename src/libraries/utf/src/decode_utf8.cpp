// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cstdint>

#include <m/utf/utf_decode.h>
#include <m/utf/decode_result.h>

namespace m
{
    namespace utf
    {
        constexpr decode_result
        decode_utf8(std::span<std::byte const> input)
        {
            // Stolen from code I wrote ... a long long time ago.
            decode_result rv{.m_char   = k_invalid_character,
                             .m_offset = (std::numeric_limits<std::size_t>::max)()};
            std::size_t   offset{};
            std::size_t   limit = input.size();

            const std::byte b1 = input[offset++];

            if ((b1 & std::byte{0x80}) == std::byte{0x00})
            {
                // We already know we have the one...
                rv.m_char = std::to_integer<char32_t>(b1);
            }
            else if ((b1 & std::byte{0xe0}) == std::byte{0xc0})
            {
                if (offset >= limit)
                {
                    rv.m_char = k_partial_encoding;
                    return rv;
                }

                const std::byte b2 = input[offset++];

                if ((b2 & std::byte{0xc0}) != std::byte{0x80})
                {
                    return rv;
                }

                rv.m_char = std::to_integer<char32_t>(b1 & std::byte{0x1f}) << 6 |
                            std::to_integer<char32_t>(b2 & std::byte{0x3f});

                // Check for non-shortest-length encoding
                if (rv.m_char < 0x00000080)
                {
                    rv.m_char = k_invalid_character;
                    return rv;
                }
            }
            else if ((b1 & std::byte{0xf0}) == std::byte{0xe0})
            {
                if (offset >= (limit - 1))
                {
                    rv.m_char = k_partial_encoding;
                    return rv;
                }

                const std::byte b2 = input[offset++];
                const std::byte b3 = input[offset++];

                if ((b2 & std::byte{0xc0}) != std::byte{0x80})
                    return rv;

                if ((b3 & std::byte{0xc0}) != std::byte{0x80})
                    return rv;

                rv.m_char = (((std::to_integer<char32_t>(b1 & std::byte{0x0f}) << 6) |
                              std::to_integer<char32_t>(b2 & std::byte{0x3f}))
                             << 6) |
                            std::to_integer<char32_t>(b3 & std::byte{0x3f});

                // Check for non-shortest-length encoding
                if (rv.m_char < 0x00000800)
                {
                    rv.m_char = k_invalid_character;
                    return rv;
                }
            }
            else if ((b1 & std::byte{0xf8}) == std::byte{0xf0})
            {
                if (offset >= (limit - 2))
                {
                    rv.m_char = k_partial_encoding;
                    return rv;
                }

                const std::byte b2 = input[offset++];
                const std::byte b3 = input[offset++];
                const std::byte b4 = input[offset++];

                if ((b2 & std::byte{0xc0}) != std::byte{0x80})
                    return rv;

                if ((b3 & std::byte{0xc0}) != std::byte{0x80})
                    return rv;

                if ((b4 & std::byte{0xc0}) != std::byte{0x80})
                    return rv;

                rv.m_char = (((((std::to_integer<char32_t>(b1 & std::byte{0x07}) << 6) |
                                std::to_integer<char32_t>(b2 & std::byte{0x3f}))
                               << 6) |
                              std::to_integer<char32_t>(b3 & std::byte{0x3f}))
                             << 6) |
                            std::to_integer<char32_t>(b4 & std::byte{0x3f});

                if (rv.m_char < 0x00010000)
                {
                    rv.m_char = k_invalid_character;
                    return rv;
                }
            }
            else if ((b1 & std::byte{0xfc}) == std::byte{0xf8})
            {
                if (offset >= (limit - 3))
                {
                    rv.m_char = k_partial_encoding;
                    return rv;
                }

                const std::byte b2 = input[offset++];
                const std::byte b3 = input[offset++];
                const std::byte b4 = input[offset++];
                const std::byte b5 = input[offset++];

                if ((b2 & std::byte{0xc0}) != std::byte{0x80})
                    return rv;

                if ((b3 & std::byte{0xc0}) != std::byte{0x80})
                    return rv;

                if ((b4 & std::byte{0xc0}) != std::byte{0x80})
                    return rv;

                if ((b5 & std::byte{0xc0}) != std::byte{0x80})
                    return rv;

                rv.m_char = (((((((std::to_integer<char32_t>(b1 & std::byte{0x03}) << 6) |
                                  std::to_integer<char32_t>(b2 & std::byte{0x3f}))
                                 << 6) |
                                std::to_integer<char32_t>(b3 & std::byte{0x3f}))
                               << 6) |
                              std::to_integer<char32_t>(b4 & std::byte{0x3f}))
                             << 6) |
                            std::to_integer<char32_t>(b5 & std::byte{0x3f});

                if (rv.m_char < 0x00200000)
                {
                    rv.m_char = k_invalid_character;
                    return rv;
                }
            }
            else if ((b1 & std::byte{0xfe}) == std::byte{0xfc})
            {
                if (offset >= (limit - 4))
                {
                    rv.m_char = k_partial_encoding;
                    return rv;
                }

                const std::byte b2 = input[offset++];
                const std::byte b3 = input[offset++];
                const std::byte b4 = input[offset++];
                const std::byte b5 = input[offset++];
                const std::byte b6 = input[offset++];

                if ((b2 & std::byte{0xc0}) != std::byte{0x80})
                    return rv;

                if ((b3 & std::byte{0xc0}) != std::byte{0x80})
                    return rv;

                if ((b4 & std::byte{0xc0}) != std::byte{0x80})
                    return rv;

                if ((b5 & std::byte{0xc0}) != std::byte{0x80})
                    return rv;

                if ((b6 & std::byte{0xc0}) != std::byte{0x80})
                    return rv;

                rv.m_char = (((((((((std::to_integer<char32_t>(b1 & std::byte{0x01}) << 6) |
                                    std::to_integer<char32_t>(b2 & std::byte{0x3f}))
                                   << 6) |
                                  std::to_integer<char32_t>(b3 & std::byte{0x3f}))
                                 << 6) |
                                std::to_integer<char32_t>(b4 & std::byte{0x3f}))
                               << 6) |
                              std::to_integer<char32_t>(b5 & std::byte{0x3f}))
                             << 6) |
                            std::to_integer<char32_t>(b6 & std::byte{0x3f});

                if (rv.m_char < 0x04000000)
                {
                    rv.m_char = k_invalid_character;
                    return rv;
                }
            }
            else
                return rv;

            rv.m_offset = offset;
            return rv;
        }

    } // namespace utf
} // namespace m