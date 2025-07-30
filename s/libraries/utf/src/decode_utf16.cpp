// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <climits>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <m/utf/decode.h>
#include <m/utf/decode_result.h>

namespace
{
    constexpr char16_t
    get_char16t_le(std::span<std::byte const> input, std::size_t& offset)
    {
        auto const b1 = input[offset++];
        auto const b2 = input[offset++];
        return std::to_integer<char16_t>(b1) | (std::to_integer<char16_t>(b2) << CHAR_BIT);
    }

    constexpr char16_t
    get_char16t_be(std::span<std::byte const> input, std::size_t& offset)
    {
        auto const b1 = input[offset++];
        auto const b2 = input[offset++];
        return std::to_integer<char16_t>(b2) | (std::to_integer<char16_t>(b1) << CHAR_BIT);
    }

} // namespace

namespace m
{
    namespace utf
    {
        decode_result
        decode_utf16le(std::span<std::byte const> input)
        {
            decode_result     rv{.m_char   = k_invalid_character,
                                 .m_offset = (std::numeric_limits<std::size_t>::max)()};
            std::size_t       offset{};
            std::size_t const limit{input.size()};

            if (limit < sizeof(char16_t))
            {
                rv.m_char = k_partial_encoding;
                return rv;
            }

            auto const ch1 = get_char16t_le(input, offset);
            if (ch1 < 0xd800)
            {
                rv.m_char = ch1;
            }
            else if (ch1 <= 0xdbff)
            {
                if (limit < (2 * sizeof(char16_t)))
                {
                    rv.m_char = k_partial_encoding;
                    return rv;
                }

                auto const ch2 = get_char16t_le(input, offset);

                if ((ch2 < 0xdc00) || (ch2 > 0xdfff))
                    return rv;

                rv.m_char = ((((ch1 - 0xd800) * 1024) + (ch2 - 0xdc00)) + 0x10000);
            }
            else if (ch1 <= 0xdfff)
            {
                // Low surrogate first... nasty!
                return rv;
            }
            else
                rv.m_char = ch1;

            rv.m_offset = offset;
            return rv;
        }

        decode_result
        decode_utf16be(std::span<std::byte const> input)
        {
            decode_result rv{.m_char   = k_invalid_character,
                             .m_offset = (std::numeric_limits<size_t>::max)()};
            size_t        offset{};
            size_t const  limit{input.size()};

            if (limit < sizeof(char16_t))
            {
                rv.m_char = k_partial_encoding;
                return rv;
            }

            auto const ch1 = get_char16t_be(input, offset);
            if (ch1 < 0xd800)
            {
                rv.m_char = ch1;
            }
            else if (ch1 <= 0xdbff)
            {
                if (limit < (2 * sizeof(char16_t)))
                    return rv;

                auto const ch2 = get_char16t_be(input, offset);

                if ((ch2 < 0xdc00) || (ch2 > 0xdfff))
                    return rv;

                rv.m_char = ((((ch1 - 0xd800) * 1024) + (ch2 - 0xdc00)) + 0x10000);
            }
            else if (ch1 <= 0xdfff)
            {
                // Low surrogate first... nasty!
                return rv;
            }
            else
                rv.m_char = ch1;

            rv.m_offset = offset;
            return rv;
        }

    } // namespace utf
} // namespace m

