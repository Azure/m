// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstddef>
#include <iostream>
#include <iterator>

#include <m/utility/concepts.h>
#include <m/utility/iterator.h>

namespace m
{
    namespace utf
    {
        template <typename OutCharT, typename OutIterT>
            requires m::character<OutCharT> && std::weakly_incrementable<OutIterT> &&
                     (sizeof(OutCharT) == 1)
        constexpr OutIterT encode_utf8(char32_t ch, OutIterT it)
        {
            using byte_t = OutCharT;

            if ((ch >= 0x110000) || ((ch >= 0xd800) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            if (ch < 0x00000080)
            {
                *it++ = static_cast<byte_t>(ch);
            }
            else if (ch < 0x00000800)
            {
                *it++ = static_cast<byte_t>(0xc0) | static_cast<byte_t>((ch >> 6) & 0x1f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 0) & 0x3f);
            }
            else if (ch < 0x00010000)
            {
                *it++ = static_cast<byte_t>(0xe0) | static_cast<byte_t>((ch >> 12) & 0x0f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 6) & 0x3f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 0) & 0x3f);
            }
            else if (ch < 0x00200000)
            {
                *it++ = static_cast<byte_t>(0xf0) | static_cast<byte_t>((ch >> 18) & 0x07);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 12) & 0x3f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 6) & 0x3f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 0) & 0x3f);
            }
            else if (ch < 0x04000000)
            {
                *it++ = static_cast<byte_t>(0xf8) | static_cast<byte_t>((ch >> 24) & 0x03);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 18) & 0x3f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 12) & 0x3f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 6) & 0x3f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 0) & 0x3f);
            }
            else
            {
                *it++ = static_cast<byte_t>(0xfc) | static_cast<byte_t>((ch >> 30) & 0x01);
                *it++ = static_cast<byte_t>(0xf8) | static_cast<byte_t>((ch >> 24) & 0x3f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 18) & 0x3f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 12) & 0x3f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 6) & 0x3f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 0) & 0x3f);
            }

            return it;
        }

        template <typename OutCharT, typename OutIterT>
            requires m::character<OutCharT> && std::weakly_incrementable<OutIterT> &&
                     (sizeof(OutCharT) == 1)
        constexpr OutIterT encode_utf8(char32_t ch, OutIterT it, std::error_code& ec)
        {
            using byte_t = OutCharT;

            if ((ch >= 0x110000) || ((ch >= 0xd800) && (ch <= 0xdfff)))
            {
                ec = std::make_error_code(std::errc::illegal_byte_sequence);
                return it;
            }

            if (ch < 0x00000080)
            {
                *it++ = static_cast<byte_t>(ch);
            }
            else if (ch < 0x00000800)
            {
                *it++ = static_cast<byte_t>(0xc0) | static_cast<byte_t>((ch >> 6) & 0x1f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 0) & 0x3f);
            }
            else if (ch < 0x00010000)
            {
                *it++ = static_cast<byte_t>(0xe0) | static_cast<byte_t>((ch >> 12) & 0x0f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 6) & 0x3f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 0) & 0x3f);
            }
            else if (ch < 0x00200000)
            {
                *it++ = static_cast<byte_t>(0xf0) | static_cast<byte_t>((ch >> 18) & 0x07);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 12) & 0x3f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 6) & 0x3f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 0) & 0x3f);
            }
            else if (ch < 0x04000000)
            {
                *it++ = static_cast<byte_t>(0xf8) | static_cast<byte_t>((ch >> 24) & 0x03);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 18) & 0x3f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 12) & 0x3f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 6) & 0x3f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 0) & 0x3f);
            }
            else
            {
                *it++ = static_cast<byte_t>(0xfc) | static_cast<byte_t>((ch >> 30) & 0x01);
                *it++ = static_cast<byte_t>(0xf8) | static_cast<byte_t>((ch >> 24) & 0x3f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 18) & 0x3f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 12) & 0x3f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 6) & 0x3f);
                *it++ = static_cast<byte_t>(0x80) | static_cast<byte_t>((ch >> 0) & 0x3f);
            }

            return it;
        }

        constexpr std::size_t
        compute_encoded_utf8_size(char32_t ch)
        {
            if ((ch >= 0x0011'0000) || ((ch >= 0xd800) && (ch <= 0xdfff)))
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

        template <typename OutIterT, typename ByteT = iterator_value_type_t<OutIterT>>
            requires(sizeof(ByteT) == 1 && std::weakly_incrementable<OutIterT> &&
                     std::is_unsigned_v<ByteT>)
        constexpr OutIterT
        write_uint16_be(uint16_t v, OutIterT it)
        {
            *it++ = static_cast<ByteT>(static_cast<uint8_t>((v >> 8) & 0xffu));
            *it++ = static_cast<ByteT>(static_cast<uint8_t>((v >> 0) & 0xffu));
            return it;
        }

        template <typename OutIterT, typename ByteT = iterator_value_type_t<OutIterT>>
            requires(sizeof(ByteT) == 1 && std::weakly_incrementable<OutIterT> &&
                     std::is_unsigned_v<ByteT>)
        constexpr OutIterT
        write_uint16_le(uint16_t v, OutIterT it)
        {
            *it++ = static_cast<ByteT>(static_cast<uint8_t>((v >> 0) & 0xffu));
            *it++ = static_cast<ByteT>(static_cast<uint8_t>((v >> 8) & 0xffu));
            return it;
        }

        template <typename OutIterT, typename ByteT = iterator_value_type_t<OutIterT>>
            requires(std::is_integral_v<ByteT> && (!std::is_same_v<ByteT, bool>) &&
                     (sizeof(ByteT) == 1) && std::weakly_incrementable<OutIterT> &&
                     std::is_unsigned_v<ByteT>)
        constexpr OutIterT
        encode_utf16le(char32_t ch, OutIterT it)
        {
            if ((ch >= 0x110000) || ((ch >= 0xd800) && (ch <= 0xdfff)))
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

        template <typename OutIterT, typename ByteT = iterator_value_type_t<OutIterT>>
            requires(std::is_integral_v<ByteT> && (!std::is_same_v<ByteT, bool>) &&
                     (sizeof(ByteT) == 1) && std::weakly_incrementable<OutIterT> &&
                     std::is_unsigned_v<ByteT>)
        constexpr OutIterT
        encode_utf16le(char32_t ch, OutIterT it, std::error_code& ec)
        {
            if ((ch >= 0x110000) || ((ch >= 0xd800) && (ch <= 0xdfff)))
            {
                ec = std::make_error_code(std::errc::illegal_byte_sequence);
                return it;
                // throw std::runtime_error("invalid character");
            }

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

        template <typename OutCharT, typename OutIterT>
            requires std::is_integral_v<OutCharT> &&
                     (sizeof(OutCharT) == 2) && std::weakly_incrementable<OutIterT>
        constexpr OutIterT encode_utf16(char32_t ch, OutIterT it)
        {
            using word_t = OutCharT;

            if ((ch >= 0x110000) || ((ch >= 0xd800) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            if (ch < 0x10000)
            {
                *it++ = static_cast<word_t>(ch);
            }
            else
            {
                *it++ = static_cast<word_t>(((ch - 0x10000) / 0x400) + 0xd800);
                *it++ = static_cast<word_t>(((ch - 0x10000) % 0x400) + 0xdc00);
            }

            return it;
        }

        constexpr std::size_t
        compute_encoded_utf16_count(char32_t ch)
        {
            if ((ch >= 0x0011'0000) || ((ch >= 0xd800) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            if (ch < 0x0001'0000)
                return 1;

            return 2;
        }

        constexpr std::size_t
        compute_encoded_utf16_count(char32_t ch, std::error_code& ec)
        {
            if ((ch >= 0x0011'0000) || ((ch >= 0xd800) && (ch <= 0xdfff)))
            {
                ec = std::make_error_code(std::errc::illegal_byte_sequence);
                return 0;
                // throw std::runtime_error("invalid character");
            }

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
        compute_encoded_utf16_bytes(char32_t ch, std::error_code& ec)
        {
            return compute_encoded_utf16_count(ch, ec) * sizeof(char16_t);
        }

        constexpr std::size_t
        compute_encoded_utf16le_count(char32_t ch)
        {
            return compute_encoded_utf16_count(ch);
        }

        constexpr std::size_t
        compute_encoded_utf16le_count(char32_t ch, std::error_code& ec)
        {
            return compute_encoded_utf16_count(ch, ec);
        }

        constexpr std::size_t
        compute_encoded_utf16le_byte(char32_t ch)
        {
            return compute_encoded_utf16_bytes(ch);
        }

        constexpr std::size_t
        compute_encoded_utf16le_byte(char32_t ch, std::error_code& ec)
        {
            return compute_encoded_utf16_bytes(ch, ec);
        }

        template <typename OutIterT, typename OutValueT = iterator_value_type_t<OutIterT>>
            requires(std::weakly_incrementable<OutIterT> && (sizeof(OutValueT) == 2) &&
                     std::is_unsigned_v<OutValueT>)
        constexpr OutIterT
        encode_utf16be(char32_t ch, OutIterT it)
        {
            if ((ch >= 0x110000) || ((ch >= 0xd800) && (ch <= 0xdfff)))
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

        template <typename OutIterT, typename OutValueT = iterator_value_type_t<OutIterT>>
            requires(std::weakly_incrementable<OutIterT> && (sizeof(OutValueT) == 2) &&
                     std::is_unsigned_v<OutValueT>)
        constexpr OutIterT
        encode_utf16be(char32_t ch, OutIterT it, std::error_code& ec)
        {
            if ((ch >= 0x110000) || ((ch >= 0xd800) && (ch <= 0xdfff)))
            {
                ec = std::make_error_code(std::errc::illegal_byte_sequence);
                return it;
                // throw std::runtime_error("invalid character");
            }

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
        compute_encoded_utf16be_bytes(char32_t ch, std::error_code& ec)
        {
            return compute_encoded_utf16_bytes(ch, ec);
        }

        constexpr std::size_t
        compute_encoded_utf16be_count(char32_t ch)
        {
            return compute_encoded_utf16_count(ch);
        }

        constexpr std::size_t
        compute_encoded_utf16be_count(char32_t ch, std::error_code& ec)
        {
            return compute_encoded_utf16_count(ch, ec);
        }

        template <typename OutIterT, typename OutValueT = iterator_value_type_t<OutIterT>>
            requires(std::weakly_incrementable<OutIterT> && (sizeof(OutValueT) == 1) &&
                     std::is_unsigned_v<OutValueT>)
        constexpr OutIterT
        encode_utf32le(char32_t ch, OutIterT it)
        {
            if ((ch >= 0x110000) || ((ch >= 0xd800) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            *it++ = OutValueT{static_cast<uint8_t>((ch >> 0) & 0xff)};
            *it++ = OutValueT{static_cast<uint8_t>((ch >> 8) & 0xff)};
            *it++ = OutValueT{static_cast<uint8_t>((ch >> 16) & 0xff)};
            *it++ = OutValueT{static_cast<uint8_t>((ch >> 24) & 0xff)};

            return it;
        }

        template <typename OutIterT, typename OutValueT = iterator_value_type_t<OutIterT>>
            requires(std::weakly_incrementable<OutIterT> && (sizeof(OutValueT) == 1) &&
                     std::is_unsigned_v<OutValueT>)
        constexpr OutIterT
        encode_utf32le(char32_t ch, OutIterT it, std::error_code& ec)
        {
            if ((ch >= 0x110000) || ((ch >= 0xd800) && (ch <= 0xdfff)))
            {
                ec = std::make_error_code(std::errc::illegal_byte_sequence);
                return it;
                // throw std::runtime_error("invalid character");
            }

            *it++ = OutValueT{static_cast<uint8_t>((ch >> 0) & 0xff)};
            *it++ = OutValueT{static_cast<uint8_t>((ch >> 8) & 0xff)};
            *it++ = OutValueT{static_cast<uint8_t>((ch >> 16) & 0xff)};
            *it++ = OutValueT{static_cast<uint8_t>((ch >> 24) & 0xff)};

            return it;
        }

        template <typename OutCharT, typename OutIterT>
            requires(m::character<OutCharT> && (sizeof(OutCharT) == 4) &&
                     std::weakly_incrementable<OutIterT>)
        constexpr OutIterT
        encode_utf32(char32_t ch, OutIterT it)
        {
            if ((ch >= 0x110000) || ((ch >= 0xd800) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            *it++ = static_cast<OutCharT>(ch);

            return it;
        }

        template <typename OutCharT, typename OutIterT>
            requires(m::character<OutCharT> && (sizeof(OutCharT) == 4) &&
                     std::weakly_incrementable<OutIterT>)
        constexpr OutIterT
        encode_utf32(char32_t ch, OutIterT it, std::error_code& ec)
        {
            if ((ch >= 0x110000) || ((ch >= 0xd800) && (ch <= 0xdfff)))
            {
                ec = std::make_error_code(std::errc::illegal_byte_sequence);
                return it;
                // throw std::runtime_error("invalid character");
            }

            *it++ = static_cast<OutCharT>(ch);

            return it;
        }

        constexpr std::size_t
        compute_encoded_utf32_bytes(char32_t ch)
        {
            if ((ch >= 0x0011'0000) || ((ch >= 0xd800) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            return 4;
        }

        constexpr std::size_t
        compute_encoded_utf32_bytes(char32_t ch, std::error_code& ec)
        {
            if ((ch >= 0x0011'0000) || ((ch >= 0xd800) && (ch <= 0xdfff)))
            {
                ec = std::make_error_code(std::errc::illegal_byte_sequence);
                return 0;
                // throw std::runtime_error("invalid character");
            }

            return 4;
        }

        constexpr std::size_t
        compute_encoded_utf32_count(char32_t ch)
        {
            if ((ch >= 0x0011'0000) || ((ch >= 0xd800) && (ch <= 0xdfff)))
                throw std::runtime_error("invalid character");

            return 1;
        }

        constexpr std::size_t
        compute_encoded_utf32_count(char32_t ch, std::error_code& ec)
        {
            if ((ch >= 0x0011'0000) || ((ch >= 0xd800) && (ch <= 0xdfff)))
            {
                ec = std::make_error_code(std::errc::illegal_byte_sequence);
                return 0;
                // throw std::runtime_error("invalid character");
            }

            return 1;
        }

        constexpr std::size_t
        compute_encoded_utf32le_bytes(char32_t ch)
        {
            return compute_encoded_utf32_bytes(ch);
        }

        constexpr std::size_t
        compute_encoded_utf32le_bytes(char32_t ch, std::error_code& ec)
        {
            return compute_encoded_utf32_bytes(ch, ec);
        }

        constexpr std::size_t
        compute_encoded_utf32be_bytes(char32_t ch)
        {
            return compute_encoded_utf32_bytes(ch);
        }

        constexpr std::size_t
        compute_encoded_utf32be_bytes(char32_t ch, std::error_code& ec)
        {
            return compute_encoded_utf32_bytes(ch, ec);
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
                     (sizeof(DestCharT) == sizeof(char8_t)) && std::is_integral_v<UcsCharT> &&
                     (sizeof(UcsCharT) == sizeof(char32_t))
        constexpr std::size_t
            compute_encoded_char_count(DestCharT, UcsCharT ch, std::error_code& ec)
        {
            return compute_encoded_utf8_count(ch, ec);
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
                     (sizeof(DestCharT) == sizeof(char16_t)) && std::is_integral_v<UcsCharT> &&
                     (sizeof(UcsCharT) == sizeof(char32_t))
        constexpr std::size_t
            compute_encoded_char_count(DestCharT, UcsCharT ch, std::error_code& ec)
        {
            return compute_encoded_utf16_count(ch, ec);
        }

        template <typename DestCharT, typename UcsCharT>
            requires(m::character<DestCharT> && m::character<UcsCharT> &&
                     (sizeof(DestCharT) == sizeof(char32_t)) &&
                     (sizeof(UcsCharT) == sizeof(char32_t)))
        constexpr std::size_t
        compute_encoded_char_count(DestCharT, UcsCharT ch)
        {
            return compute_encoded_utf32_count(ch);
        }

        template <typename DestCharT, typename UcsCharT>
            requires(m::character<DestCharT> && m::character<UcsCharT> &&
                     (sizeof(DestCharT) == sizeof(char32_t)) &&
                     (sizeof(UcsCharT) == sizeof(char32_t)))
        constexpr std::size_t
        compute_encoded_char_count(DestCharT, UcsCharT ch, std::error_code& ec)
        {
            return compute_encoded_utf32_count(ch, ec);
        }

        template <typename OutCharT, typename OutIterT>
            requires(m::character<OutCharT> && std::weakly_incrementable<OutIterT> /* &&
                     std::is_same_v<iterator_value_type_t<OutIterT>, char8_t> */
                     )
        constexpr OutIterT
        encode_char(char32_t ch, OutIterT it)
        {
            using out_t = OutCharT; // remove_cvref_t<decltype(*it)>;

            if constexpr (sizeof(out_t) == 1)
            {
                return encode_utf8<OutCharT>(ch, it);
            }
            else if constexpr (sizeof(out_t) == 2)
            {
                return encode_utf16<OutCharT>(ch, it);
            }
            else if constexpr (sizeof(out_t) == 4)
            {
                return encode_utf32<OutCharT>(ch, it);
            }
            else
            {
                std::cerr << "Broken output type is: " << typeid(out_t).name() << "\n";
                throw std::logic_error("Not possible!");
            }
        }

        template <typename OutCharT, typename OutIterT>
            requires(m::character<OutCharT> && std::weakly_incrementable<OutIterT> /* &&
                     std::is_same_v<iterator_value_type_t<OutIterT>, char8_t> */
                     )
        constexpr OutIterT
        encode_char(char32_t ch, OutIterT it, std::error_code& ec)
        {
            using out_t = OutCharT; // remove_cvref_t<decltype(*it)>;

            if constexpr (sizeof(out_t) == 1)
            {
                return encode_utf8<OutCharT>(ch, it, ec);
            }
            else if constexpr (sizeof(out_t) == 2)
            {
                return encode_utf16<OutCharT>(ch, it, ec);
            }
            else if constexpr (sizeof(out_t) == 4)
            {
                return encode_utf32<OutCharT>(ch, it, ec);
            }
            else
            {
                std::cerr << "Broken output type is: " << typeid(out_t).name() << "\n";
                throw std::logic_error("Not possible!");
            }
        }

    } // namespace utf
} // namespace m
