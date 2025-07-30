// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstddef>
#include <iterator>

namespace m
{
    namespace utf
    {
        template <typename OutIterT, typename ByteT = std::iterator_traits<OutIterT>::value_type>
            requires(sizeof(ByteT) == 1) && std::output_iterator<OutIterT, ByteT>
        constexpr OutIterT
        encode_utf8(char32_t ch, OutIterT it)
        {
            if ((ch >= 0x110000) || ((ch >= 0xdc00) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            if (ch < 0x00000080)
            {
                *it++ = static_cast<ByteT>(ch);
            }
            else if (ch < 0x00000800)
            {
                *it++ =
                    static_cast<ByteT>(std::byte{0xc0} | static_cast<std::byte>((ch >> 6) & 0x1f));
                *it++ =
                    static_cast<ByteT>(std::byte{0x80} | static_cast<std::byte>((ch >> 0) & 0x3f));
            }
            else if (ch < 0x00010000)
            {
                *it++ =
                    static_cast<ByteT>(std::byte{0xe0} | static_cast<std::byte>((ch >> 12) & 0x0f));
                *it++ =
                    static_cast<ByteT>(std::byte{0x80} | static_cast<std::byte>((ch >> 6) & 0x3f));
                *it++ =
                    static_cast<ByteT>(std::byte{0x80} | static_cast<std::byte>((ch >> 0) & 0x3f));
            }
            else if (ch < 0x00200000)
            {
                *it++ =
                    static_cast<ByteT>(std::byte{0xf0} | static_cast<std::byte>((ch >> 18) & 0x07));
                *it++ =
                    static_cast<ByteT>(std::byte{0x80} | static_cast<std::byte>((ch >> 12) & 0x3f));
                *it++ =
                    static_cast<ByteT>(std::byte{0x80} | static_cast<std::byte>((ch >> 6) & 0x3f));
                *it++ =
                    static_cast<ByteT>(std::byte{0x80} | static_cast<std::byte>((ch >> 0) & 0x3f));
            }
            else if (ch < 0x04000000)
            {
                *it++ =
                    static_cast<ByteT>(std::byte{0xf8} | static_cast<std::byte>((ch >> 24) & 0x03));
                *it++ =
                    static_cast<ByteT>(std::byte{0x80} | static_cast<std::byte>((ch >> 18) & 0x3f));
                *it++ =
                    static_cast<ByteT>(std::byte{0x80} | static_cast<std::byte>((ch >> 12) & 0x3f));
                *it++ =
                    static_cast<ByteT>(std::byte{0x80} | static_cast<std::byte>((ch >> 6) & 0x3f));
                *it++ =
                    static_cast<ByteT>(std::byte{0x80} | static_cast<std::byte>((ch >> 0) & 0x3f));
            }
            else
            {
                *it++ =
                    static_cast<ByteT>(std::byte{0xfc} | static_cast<std::byte>((ch >> 30) & 0x01));
                *it++ =
                    static_cast<ByteT>(std::byte{0xf8} | static_cast<std::byte>((ch >> 24) & 0x3f));
                *it++ =
                    static_cast<ByteT>(std::byte{0x80} | static_cast<std::byte>((ch >> 18) & 0x3f));
                *it++ =
                    static_cast<ByteT>(std::byte{0x80} | static_cast<std::byte>((ch >> 12) & 0x3f));
                *it++ =
                    static_cast<ByteT>(std::byte{0x80} | static_cast<std::byte>((ch >> 6) & 0x3f));
                *it++ =
                    static_cast<ByteT>(std::byte{0x80} | static_cast<std::byte>((ch >> 0) & 0x3f));
            }

            return it;
        }

        template <typename OutIterT, typename ByteT = std::iterator_traits<OutIterT>::value_type>
            requires std::weakly_incrementable<OutIterT> // && std::output_iterator<OutIterT, ByteT>
        constexpr OutIterT
        encode_utf8(char32_t ch, OutIterT it)
        {
            if ((ch >= 0x110000) || ((ch >= 0xdc00) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            if (ch < 0x00000080)
            {
                *it++ = static_cast<ByteT>(ch);
            }
            else if (ch < 0x00000800)
            {
                *it++ = ByteT{0xc0} | static_cast<ByteT>((ch >> 6) & 0x1f);
                *it++ = ByteT{0x80} | static_cast<ByteT>((ch >> 0) & 0x3f);
            }
            else if (ch < 0x00010000)
            {
                *it++ = ByteT{0xe0} | static_cast<ByteT>((ch >> 12) & 0x0f);
                *it++ = ByteT{0x80} | static_cast<ByteT>((ch >> 6) & 0x3f);
                *it++ = ByteT{0x80} | static_cast<ByteT>((ch >> 0) & 0x3f);
            }
            else if (ch < 0x00200000)
            {
                *it++ = ByteT{0xf0} | static_cast<ByteT>((ch >> 18) & 0x07);
                *it++ = ByteT{0x80} | static_cast<ByteT>((ch >> 12) & 0x3f);
                *it++ = ByteT{0x80} | static_cast<ByteT>((ch >> 6) & 0x3f);
                *it++ = ByteT{0x80} | static_cast<ByteT>((ch >> 0) & 0x3f);
            }
            else if (ch < 0x04000000)
            {
                *it++ = ByteT{0xf8} | static_cast<ByteT>((ch >> 24) & 0x03);
                *it++ = ByteT{0x80} | static_cast<ByteT>((ch >> 18) & 0x3f);
                *it++ = ByteT{0x80} | static_cast<ByteT>((ch >> 12) & 0x3f);
                *it++ = ByteT{0x80} | static_cast<ByteT>((ch >> 6) & 0x3f);
                *it++ = ByteT{0x80} | static_cast<ByteT>((ch >> 0) & 0x3f);
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

            return it;
        }

        // crazy signature to enable selection via overload. First parameter not used.
        template <typename ByteT, typename OutIterT>
            requires std::output_iterator<OutIterT, ByteT>
        constexpr OutIterT
        encode_utf8(ByteT, char32_t ch, OutIterT it)
        {
            return encode_utf8<OutIterT, ByteT>(ch, it);
        }

        template <typename UtcCharT>
            requires std::is_integral_v<UtcCharT> && (sizeof(UtcCharT) == 4)
        constexpr std::size_t compute_encoded_utf8_size(UtcCharT ch)
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

        constexpr std::size_t
        compute_encoded_utf8_count(char32_t ch)
        {
            return compute_encoded_utf8_size(ch);
        }

        template <typename OutIterT, typename ByteT = std::iterator_traits<OutIterT>::value_type>
            requires(sizeof(ByteT) == 1) // && std::output_iterator<OutIterT, ByteT>
        constexpr OutIterT
        write_uint16_be(uint16_t v, OutIterT it)
        {
            *it++ = static_cast<ByteT>(static_cast<uint8_t>((v >> 8) & 0xffu));
            *it++ = static_cast<ByteT>(static_cast<uint8_t>((v >> 0) & 0xffu));
            return it;
        }

        template <typename OutIterT, typename ByteT = std::iterator_traits<OutIterT>::value_type>
            requires(sizeof(ByteT) == 1) // && std::output_iterator<OutIterT, ByteT>
        constexpr OutIterT
        write_uint16_le(uint16_t v, OutIterT it)
        {
            *it++ = static_cast<ByteT>(static_cast<uint8_t>((v >> 0) & 0xffu));
            *it++ = static_cast<ByteT>(static_cast<uint8_t>((v >> 8) & 0xffu));
            return it;
        }

        template <typename OutIterT, typename ByteT = std::iterator_traits<OutIterT>::value_type>
            requires std::is_integral_v<ByteT> && (!std::is_same_v<ByteT, bool>) && (sizeof(ByteT) == 1) &&
                     std::weakly_incrementable<OutIterT> // && std::output_iterator<OutIterT, ByteT>
        constexpr OutIterT encode_utf16le(char32_t ch, OutIterT it)
        {
            if ((ch >= 0x110000) || ((ch >= 0xdc00) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            if (ch < 0x10000)
            {
                it = write_uint16_le(static_cast<uint16_t>(ch), it);
            }
            else
            {
                it = write_uint16_le(static_cast<uint16_t>(((ch - 0x10000) / 0x400) + 0xd800), it);
                it = write_uint16_le(static_cast<uint16_t>(((ch - 0x10000) % 0x400) + 0xdc00), it);
            }

            return it;
        }

        template <typename OutIterT, typename WordT = std::iterator_traits<OutIterT>::value_type>
            requires std::is_integral_v<WordT> && (sizeof(WordT) == 2) &&
                     std::weakly_incrementable<OutIterT> // && std::output_iterator<OutIterT, WordT>
        constexpr OutIterT encode_utf16(char32_t ch, OutIterT it)
        {
            if ((ch >= 0x110000) || ((ch >= 0xdc00) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            if (ch < 0x10000)
            {
                *it++ = static_cast<WordT>(ch);
            }
            else
            {
                *it++ = static_cast<WordT>(((ch - 0x10000) / 0x400) + 0xd800);
                *it++ = static_cast<WordT>(((ch - 0x10000) % 0x400) + 0xdc00);
            }

            return it;
        }

        template <typename WordT, typename OutIterT>
            requires std::is_integral_v<WordT> &&
                     (sizeof(WordT) == 2) && std::output_iterator<OutIterT, WordT>
        constexpr OutIterT encode_utf16(WordT, char32_t ch, OutIterT it)
        {
            return encode_utf16<OutIterT, WordT>(ch, it);
        }

        constexpr std::size_t
        compute_encoded_utf16_count(char32_t ch)
        {
            if ((ch >= 0x0011'0000) || ((ch >= 0xdc00) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            if (ch < 0x0001'0000)
                return 1;

            return 2;
        }

        constexpr std::size_t
        compute_encoded_utf16_bytes(char32_t ch)
        {
            return compute_encoded_utf16_count(ch) * sizeof(char16_t);
        }

        constexpr std::size_t
        compute_encoded_utf16le_count(char32_t ch)
        {
            return compute_encoded_utf16_count(ch);
        }

        constexpr std::size_t
        compute_encoded_utf16le_byte(char32_t ch)
        {
            return compute_encoded_utf16_bytes(ch);
        }

        template <typename OutIterT, typename ByteT = std::iterator_traits<OutIterT>::value_type>
            requires std::output_iterator<OutIterT, ByteT>
        constexpr OutIterT
        encode_utf16be(char32_t ch, OutIterT it)
        {
            if ((ch >= 0x110000) || ((ch >= 0xdc00) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            if (ch < 0x10000)
            {
                it = write_uint16_be(static_cast<uint16_t>(ch), it);
            }
            else if (ch < 0x110000)
            {
                it = write_uint16_be(static_cast<uint16_t>(((ch - 0x10000) / 0x400) + 0xd800), it);
                it = write_uint16_be(static_cast<uint16_t>(((ch - 0x10000) % 0x400) + 0xdc00), it);
            }

            return it;
        }

        constexpr std::size_t
        compute_encoded_utf16be_bytes(char32_t ch)
        {
            return compute_encoded_utf16_bytes(ch);
        }

        constexpr std::size_t
        compute_encoded_utf16be_count(char32_t ch)
        {
            return compute_encoded_utf16_count(ch);
        }

        template <typename OutIterT, typename ByteT = std::iterator_traits<OutIterT>::value_type>
            requires std::output_iterator<OutIterT, ByteT>
        constexpr OutIterT
        encode_utf32le(char32_t ch, OutIterT it)
        {
            if ((ch >= 0x110000) || ((ch >= 0xdc00) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            *it++ = ByteT{static_cast<uint8_t>((ch >> 0) & 0xff)};
            *it++ = ByteT{static_cast<uint8_t>((ch >> 8) & 0xff)};
            *it++ = ByteT{static_cast<uint8_t>((ch >> 16) & 0xff)};
            *it++ = ByteT{static_cast<uint8_t>((ch >> 24) & 0xff)};

            return it;
        }

        template <typename OutIterT, typename DwordT = std::iterator_traits<OutIterT>::value_type>
            requires std::is_integral_v<DwordT> &&
                     (sizeof(DwordT) == 4) && std::output_iterator<OutIterT, DwordT>
        constexpr OutIterT encode_utf32(char32_t ch, OutIterT it)
        {
            if ((ch >= 0x110000) || ((ch >= 0xdc00) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            *it++ = static_cast<DwordT>(ch);

            return it;
        }

        template <typename DwordT, typename OutIterT>
            requires std::is_integral_v<DwordT> &&
                     (sizeof(DwordT) == 4) && std::output_iterator<OutIterT, DwordT>
        constexpr OutIterT encode_utf32(DwordT, char32_t ch, OutIterT it)
        {
            return encode_utf32<OutIterT, DwordT>(ch, it);
        }

        constexpr std::size_t
        compute_encoded_utf32_bytes(char32_t ch)
        {
            if ((ch >= 0x0011'0000) || ((ch >= 0xdc00) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            return 4;
        }

        constexpr std::size_t
        compute_encoded_utf32_count(char32_t ch)
        {
            if ((ch >= 0x0011'0000) || ((ch >= 0xdc00) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            return 1;
        }

        constexpr std::size_t
        compute_encoded_utf32le_bytes(char32_t ch)
        {
            return compute_encoded_utf32_bytes(ch);
        }

        constexpr std::size_t
        compute_encoded_utf32be_bytes(char32_t ch)
        {
            return compute_encoded_utf32_bytes(ch);
        }

        template <typename DestCharT, typename UcsCharT>
            requires std::is_integral_v<DestCharT> &&
                     (sizeof(DestCharT) == sizeof(char8_t)) && std::is_integral_v<UcsCharT> &&
                     (sizeof(UcsCharT) == sizeof(char32_t))
        constexpr std::size_t compute_encoded_char_count(DestCharT, UcsCharT ch)
        {
            return compute_encoded_utf8_count(ch);
        }

        template <typename DestCharT, typename UcsCharT>
            requires std::is_integral_v<DestCharT> &&
                     (sizeof(DestCharT) == sizeof(char16_t)) && std::is_integral_v<UcsCharT> &&
                     (sizeof(UcsCharT) == sizeof(char32_t))
        constexpr std::size_t compute_encoded_char_count(DestCharT, UcsCharT ch)
        {
            return compute_encoded_utf16_count(ch);
        }

        template <typename DestCharT, typename UcsCharT>
            requires std::is_integral_v<DestCharT> &&
                     (sizeof(DestCharT) == sizeof(char32_t)) && std::is_integral_v<UcsCharT> &&
                     (sizeof(UcsCharT) == sizeof(char32_t))
        constexpr std::size_t compute_encoded_char_count(DestCharT, UcsCharT ch)
        {
            return compute_encoded_utf32_count(ch);
        }

        template <typename DestCharT, typename UcsCharT, typename OutIterT>
            requires std::is_integral_v<DestCharT> &&
                     (sizeof(DestCharT) == sizeof(char8_t)) && std::is_integral_v<UcsCharT> &&
                     (sizeof(UcsCharT) == sizeof(char32_t)) &&
                     std::output_iterator<OutIterT, DestCharT>
        constexpr OutIterT encode_char(DestCharT, UcsCharT ch, OutIterT it)
        {
            return encode_utf8(DestCharT{}, ch, it);
        }

        template <typename DestCharT, typename UcsCharT, typename OutIterT>
            requires std::is_integral_v<DestCharT> &&
                     (sizeof(DestCharT) == sizeof(char16_t)) && std::is_integral_v<UcsCharT> &&
                     (sizeof(UcsCharT) == sizeof(char32_t)) &&
                     std::output_iterator<OutIterT, DestCharT>
        constexpr OutIterT encode_char(DestCharT, UcsCharT ch, OutIterT it)
        {
            return encode_utf16(DestCharT{}, ch, it);
        }

        template <typename DestCharT, typename UcsCharT, typename OutIterT>
            requires std::is_integral_v<DestCharT> &&
                     (sizeof(DestCharT) == sizeof(char32_t)) && std::is_integral_v<UcsCharT> &&
                     (sizeof(UcsCharT) == sizeof(char32_t)) &&
                     std::output_iterator<OutIterT, DestCharT>
        constexpr OutIterT encode_char(DestCharT, UcsCharT ch, OutIterT it)
        {
            return encode_utf32(DestCharT{}, ch, it);
        }

    } // namespace utf
} // namespace m
