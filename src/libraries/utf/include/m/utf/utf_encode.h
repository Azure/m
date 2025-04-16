// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstddef>
#include <iterator>

namespace m
{
    namespace utf
    {
        template <typename OutIter>
        constexpr void
        encode_utf8(char32_t ch, OutIter it)
            requires std::output_iterator<OutIter, std::byte>
        {
            if ((ch >= 0x110000) || ((ch >= 0xdc00) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            if (ch < 0x00000080)
            {
                *it++ = static_cast<std::byte>(ch);
            }
            else if (ch < 0x00000800)
            {
                *it++ = std::byte{0xc0} | static_cast<std::byte>((ch >> 6) & 0x1f);
                *it++ = std::byte{0x80} | static_cast<std::byte>((ch >> 0) & 0x3f);
            }
            else if (ch < 0x00010000)
            {
                *it++ = std::byte{0xe0} | static_cast<std::byte>((ch >> 12) & 0x0f);
                *it++ = std::byte{0x80} | static_cast<std::byte>((ch >> 6) & 0x3f);
                *it++ = std::byte{0x80} | static_cast<std::byte>((ch >> 0) & 0x3f);
            }
            else if (ch < 0x00200000)
            {
                *it++ = std::byte{0xf0} | static_cast<std::byte>((ch >> 18) & 0x07);
                *it++ = std::byte{0x80} | static_cast<std::byte>((ch >> 12) & 0x3f);
                *it++ = std::byte{0x80} | static_cast<std::byte>((ch >> 6) & 0x3f);
                *it++ = std::byte{0x80} | static_cast<std::byte>((ch >> 0) & 0x3f);
            }
            else if (ch < 0x04000000)
            {
                *it++ = std::byte{0xf8} | static_cast<std::byte>((ch >> 24) & 0x03);
                *it++ = std::byte{0x80} | static_cast<std::byte>((ch >> 18) & 0x3f);
                *it++ = std::byte{0x80} | static_cast<std::byte>((ch >> 12) & 0x3f);
                *it++ = std::byte{0x80} | static_cast<std::byte>((ch >> 6) & 0x3f);
                *it++ = std::byte{0x80} | static_cast<std::byte>((ch >> 0) & 0x3f);
            }
            else
            {
                *it++ = std::byte{0xfc} | static_cast<std::byte>((ch >> 30) & 0x01);
                *it++ = std::byte{0xf8} | static_cast<std::byte>((ch >> 24) & 0x3f);
                *it++ = std::byte{0x80} | static_cast<std::byte>((ch >> 18) & 0x3f);
                *it++ = std::byte{0x80} | static_cast<std::byte>((ch >> 12) & 0x3f);
                *it++ = std::byte{0x80} | static_cast<std::byte>((ch >> 6) & 0x3f);
                *it++ = std::byte{0x80} | static_cast<std::byte>((ch >> 0) & 0x3f);
            }
        }

        template <typename OutIter>
        constexpr void
        encode_utf8(char32_t ch, OutIter it)
            requires std::output_iterator<OutIter, char8_t>
        {
            if ((ch >= 0x110000) || ((ch >= 0xdc00) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            if (ch < 0x00000080)
            {
                *it++ = static_cast<char8_t>(ch);
            }
            else if (ch < 0x00000800)
            {
                *it++ = char8_t{0xc0} | static_cast<char8_t>((ch >> 6) & 0x1f);
                *it++ = char8_t{0x80} | static_cast<char8_t>((ch >> 0) & 0x3f);
            }
            else if (ch < 0x00010000)
            {
                *it++ = char8_t{0xe0} | static_cast<char8_t>((ch >> 12) & 0x0f);
                *it++ = char8_t{0x80} | static_cast<char8_t>((ch >> 6) & 0x3f);
                *it++ = char8_t{0x80} | static_cast<char8_t>((ch >> 0) & 0x3f);
            }
            else if (ch < 0x00200000)
            {
                *it++ = char8_t{0xf0} | static_cast<char8_t>((ch >> 18) & 0x07);
                *it++ = char8_t{0x80} | static_cast<char8_t>((ch >> 12) & 0x3f);
                *it++ = char8_t{0x80} | static_cast<char8_t>((ch >> 6) & 0x3f);
                *it++ = char8_t{0x80} | static_cast<char8_t>((ch >> 0) & 0x3f);
            }
            else if (ch < 0x04000000)
            {
                *it++ = char8_t{0xf8} | static_cast<char8_t>((ch >> 24) & 0x03);
                *it++ = char8_t{0x80} | static_cast<char8_t>((ch >> 18) & 0x3f);
                *it++ = char8_t{0x80} | static_cast<char8_t>((ch >> 12) & 0x3f);
                *it++ = char8_t{0x80} | static_cast<char8_t>((ch >> 6) & 0x3f);
                *it++ = char8_t{0x80} | static_cast<char8_t>((ch >> 0) & 0x3f);
            }
            else
            {
                *it++ = char8_t{0xfc} | static_cast<char8_t>((ch >> 30) & 0x01);
                *it++ = char8_t{0xf8} | static_cast<char8_t>((ch >> 24) & 0x3f);
                *it++ = char8_t{0x80} | static_cast<char8_t>((ch >> 18) & 0x3f);
                *it++ = char8_t{0x80} | static_cast<char8_t>((ch >> 12) & 0x3f);
                *it++ = char8_t{0x80} | static_cast<char8_t>((ch >> 6) & 0x3f);
                *it++ = char8_t{0x80} | static_cast<char8_t>((ch >> 0) & 0x3f);
            }
        }

        constexpr std::size_t
        compute_encoded_utf8_size(char32_t ch)
        {
            if ((ch >= 0x0011'0000) || ((ch >= 0xdc00) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            if (ch < 0x0000'0080)
                return 1;

            if (ch < 0x0000'0800)
                return 2;

            if (ch < 0x0001'0000)
                return 3;

            return 4;
        }

        template <typename OutIter>
        constexpr void
        write_uint16_be(uint16_t v, OutIter it)
            requires std::output_iterator<OutIter, std::byte>
        {
            *it++ = std::byte{static_cast<uint8_t>((v >> 8) & 0xffu)};
            *it++ = std::byte{static_cast<uint8_t>((v >> 0) & 0xffu)};
        }

        template <typename OutIter>
        constexpr void
        write_uint16_le(uint16_t v, OutIter it)
            requires std::output_iterator<OutIter, std::byte>
        {
            *it++ = std::byte{static_cast<uint8_t>((v >> 0) & 0xffu)};
            *it++ = std::byte{static_cast<uint8_t>((v >> 8) & 0xffu)};
        }

        template <typename OutIter>
        constexpr void
        encode_utf16le(char32_t ch, OutIter it)
            requires std::output_iterator<OutIter, std::byte>
        {
            if ((ch >= 0x110000) || ((ch >= 0xdc00) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            if (ch < 0x10000)
            {
                write_uint16_le(static_cast<uint16_t>(ch), it);
            }
            else
            {
                write_uint16_le(static_cast<uint16_t>(((ch - 0x10000) / 0x400) + 0xd800), it);
                write_uint16_le(static_cast<uint16_t>(((ch - 0x10000) % 0x400) + 0xdc00), it);
            }
        }

        template <typename OutIter>
        constexpr void
        encode_utf16(char32_t ch, OutIter it)
            requires std::output_iterator<OutIter, char16_t>
        {
            if ((ch >= 0x110000) || ((ch >= 0xdc00) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            if (ch < 0x10000)
            {
                *it++ = static_cast<char16_t>(ch);
            }
            else
            {
                *it++ = static_cast<char16_t>(((ch - 0x10000) / 0x400) + 0xd800);
                *it++ = static_cast<char16_t>(((ch - 0x10000) % 0x400) + 0xdc00);
            }
        }

        constexpr std::size_t
        compute_encoded_utf16_size(char32_t ch)
        {
            if ((ch >= 0x0011'0000) || ((ch >= 0xdc00) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            if (ch < 0x0001'0000)
                return 2;

            return 4;
        }

        constexpr std::size_t
        compute_encoded_utf16le_size(char32_t ch)
        {
            return compute_encoded_utf16_size(ch);
        }

        template <typename OutIter>
        constexpr void
        encode_utf16be(char32_t ch, OutIter it)
            requires std::output_iterator<OutIter, std::byte>
        {
            if ((ch >= 0x110000) || ((ch >= 0xdc00) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            if (ch < 0x10000)
            {
                write_uint16_be(static_cast<uint16_t>(ch), it);
            }
            else if (ch < 0x110000)
            {
                write_uint16_be(static_cast<uint16_t>(((ch - 0x10000) / 0x400) + 0xd800), it);
                write_uint16_be(static_cast<uint16_t>(((ch - 0x10000) % 0x400) + 0xdc00), it);
            }
        }

        constexpr std::size_t
        compute_encoded_utf16be_size(char32_t ch)
        {
            return compute_encoded_utf16_size(ch);
        }

        template <typename OutIter>
        constexpr void
        encode_utf32le(char32_t ch, OutIter it)
            requires std::output_iterator<OutIter, std::byte>
        {
            if ((ch >= 0x110000) || ((ch >= 0xdc00) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            *it++ = std::byte{static_cast<uint8_t>((ch >> 0) & 0xff)};
            *it++ = std::byte{static_cast<uint8_t>((ch >> 8) & 0xff)};
            *it++ = std::byte{static_cast<uint8_t>((ch >> 16) & 0xff)};
            *it++ = std::byte{static_cast<uint8_t>((ch >> 24) & 0xff)};
        }

        template <typename OutIter>
        constexpr void
        encode_utf32(char32_t ch, OutIter it)
            requires std::output_iterator<OutIter, char32_t>
        {
            if ((ch >= 0x110000) || ((ch >= 0xdc00) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            *it++ = ch;
        }

        constexpr std::size_t
        compute_encoded_utf32_size(char32_t ch)
        {
            if ((ch >= 0x0011'0000) || ((ch >= 0xdc00) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            return 4;
        }

        constexpr std::size_t
        compute_encoded_utf32le_size(char32_t ch)
        {
            return compute_encoded_utf32_size(ch);
        }

        constexpr std::size_t
        compute_encoded_utf32be_size(char32_t ch)
        {
            return compute_encoded_utf32_size(ch);
        }

    } // namespace utf
} // namespace m
