// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <climits>
#include <cstdint>
#include <limits>

#include <m/utf/decode_result.h>
#include <m/utf/utf_decode.h>

namespace
{
    constexpr char32_t
    get_char32t_le(std::span<std::byte const> input, std::size_t& offset)
    {
        auto const b1 = input[offset++];
        auto const b2 = input[offset++];
        auto const b3 = input[offset++];
        auto const b4 = input[offset++];
        auto const v1 = std::to_integer<char32_t>(b1) << (CHAR_BIT * 0);
        auto const v2 = std::to_integer<char32_t>(b2) << (CHAR_BIT * 1);
        auto const v3 = std::to_integer<char32_t>(b3) << (CHAR_BIT * 2);
        auto const v4 = std::to_integer<char32_t>(b4) << (CHAR_BIT * 3);
        return v1 | v2 | v3 | v4;
    }

    constexpr char32_t
    get_char32t_be(std::span<std::byte const> input, std::size_t& offset)
    {
        auto const b1 = input[offset++];
        auto const b2 = input[offset++];
        auto const b3 = input[offset++];
        auto const b4 = input[offset++];
        auto const v1 = std::to_integer<char32_t>(b4) << (CHAR_BIT * 0);
        auto const v2 = std::to_integer<char32_t>(b3) << (CHAR_BIT * 1);
        auto const v3 = std::to_integer<char32_t>(b2) << (CHAR_BIT * 2);
        auto const v4 = std::to_integer<char32_t>(b1) << (CHAR_BIT * 3);
        return v1 | v2 | v3 | v4;
    }
} // namespace

constexpr m::utf::decode_result
m::utf::decode_utf32(std::span<std::byte const> input)
{
    decode_result     rv{.m_char   = k_invalid_character,
                         .m_offset = (std::numeric_limits<std::size_t>::max)()};
    std::size_t       offset{};
    std::size_t const limit{input.size()};

    if (limit < sizeof(char32_t))
    {
        rv.m_char = k_partial_encoding;
        return rv;
    }

    auto const ch = get_char32t_le(input, offset);

    // Disallow surrogates
    if (ch >= 0xd800 && ch <= 0xdfff)
    {
        return rv;
    }

    // Utf-32 only encodes through U+10FFFF
    if (ch > 0x10ffff)
        return rv;

    rv.m_char   = ch;
    rv.m_offset = offset;
    return rv;
}
