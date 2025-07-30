// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

struct utf_data_set
{
    utf_data_set(std::initializer_list<char8_t>  utf8data,
                 std::initializer_list<char16_t> utf16data,
                 std::initializer_list<char32_t> utf32data):
        m_u8_chardata(utf8data.begin(), utf8data.end()),
        m_u16le_chardata(utf16data.begin(), utf16data.end()),
        m_u32_chardata(utf32data.begin(), utf32data.end())
    {
        std::ranges::transform(
            m_u16le_chardata, std::back_inserter(m_u16be_chardata), [](char16_t ch) -> char16_t {
                return ((ch & 0xff) << 8) | ((ch >> 8) & 0xff);
            });


        m_u8_sv    = std::u8string_view(m_u8_chardata.begin(), m_u8_chardata.end());
        m_u16le_sv = std::u16string_view(m_u16le_chardata.begin(), m_u16le_chardata.end());
        m_u16be_sv = std::u16string_view(m_u16be_chardata.begin(), m_u16be_chardata.end());
        m_u32_sv   = std::u32string_view(m_u32_chardata.begin(), m_u32_chardata.end());

        m_u8_byte_data = std::as_bytes(std::span(m_u8_chardata.begin(), m_u8_chardata.end()));
        m_u16le_byte_data =
            std::as_bytes(std::span(m_u16le_chardata.begin(), m_u16le_chardata.end()));
        m_u16be_byte_data =
            std::as_bytes(std::span(m_u16be_chardata.begin(), m_u16be_chardata.end()));
        m_u32_byte_data  = std::as_bytes(std::span(m_u32_chardata.begin(), m_u32_chardata.end()));
    }

    // Not using basic_string<> because the data may be invalid and we don't
    // want string-ish interpretation of the data ever. These are sequences
    // of char8_ts, char16_ts and char32_ts.
    std::vector<char8_t>  m_u8_chardata;
    std::vector<char16_t> m_u16le_chardata;
    std::vector<char16_t> m_u16be_chardata; // byte swapped
    std::vector<char32_t> m_u32_chardata;

    std::span<std::byte const> m_u8_byte_data;
    std::span<std::byte const> m_u16le_byte_data;
    std::span<std::byte const> m_u16be_byte_data;
    std::span<std::byte const> m_u32_byte_data;

    std::u8string_view  m_u8_sv;
    std::u16string_view m_u16le_sv;
    std::u16string_view m_u16be_sv;
    std::u32string_view m_u32_sv;
};

// Zero length data to catch those empty-set failure kinds of errors
static inline utf_data_set empty_data({}, {}, {});

static inline utf_data_set
    hellodata({char8_t{'h'}, char8_t{'e'}, char8_t{'l'}, char8_t{'l'}, char8_t{'o'}},
              {char16_t{'h'}, char16_t{'e'}, char16_t{'l'}, char16_t{'l'}, char16_t{'o'}},
              {char32_t{'h'}, char32_t{'e'}, char32_t{'l'}, char32_t{'l'}, char32_t{'o'}});

// The character sequence U+0041 U+2262 U+0391 U+002E "A<NOT IDENTICAL
//    TO><ALPHA>." is encoded in UTF-8 as follows:

static inline utf_data_set rfc3629_ex_1({char8_t{0x41},
                  char8_t{0xe2},
                  char8_t{0x89},
                  char8_t{0xa2},
                  char8_t{0xce},
                  char8_t{0x91},
                  char8_t{0x2e}},
                 {char16_t{0x41}, char16_t{0x2262}, char16_t{0x391}, char16_t{0x2e}},
                 {char32_t{0x41}, char32_t{0x2262}, char32_t{0x391}, char32_t{0x2e}});

// The character sequence U+D55C U+AD6D U+C5B4 (Korean "hangugeo",
// meaning "the Korean language") is encoded in UTF-8 as follows:
//
//    --------+--------+--------
//    ED 95 9C EA B5 AD EC 96 B4
//    --------+--------+--------

static inline utf_data_set rfc3629_ex_2(
    {0xed, 0x95, 0x9c, 0xea, 0xb5, 0xad, 0xec, 0x96, 0xb4},
                                        {0xd55c, 0xad6d, 0xc5b4}, {0xd55c, 0xad6d, 0xc5b4});

// The character sequence U+65E5 U+672C U+8A9E (Japanese "nihongo",
// meaning "the Japanese language") is encoded in UTF-8 as follows:
//
//    --------+--------+--------
//    E6 97 A5 E6 9C AC E8 AA 9E
//    --------+--------+--------

static inline utf_data_set rfc3629_ex_3({0xe6, 0x97, 0xa5, 0xe6, 0x9c, 0xac, 0xe8, 0xaa, 0x9e},
                                        {0x65e5, 0x672c, 0x8a9e}, {0x65e5, 0x672c, 0x8a9e});

// The character U+233B4 (a Chinese character meaning 'stump of tree'),
// prepended with a UTF-8 BOM, is encoded in UTF-8 as follows:
//
//    --------+-----------
//    EF BB BF F0 A3 8E B4
//    --------+-----------

static inline utf_data_set rfc3629_ex_4({0xef, 0xbb, 0xbf, 0xf0, 0xa3, 0x8e, 0xb4},
                                        {char16_t{0xfeff}, char16_t{0xd84c}, char16_t{0xdfb4}},
                                        {char32_t{0xfeff}, char32_t{0x233b4}});

//
// All those examples were well formed now we need some non-well-formed examples
//

// Lead byte, no subsequent byte. Also, 0xc0 is not a legal character
// Fails due to truncation first though
static inline std::array utf8_malformed_c0{char8_t{0b11000000}};

// Lead byte, no subsequent byte. Also, 0xc1 is not a legal character
// Fails due to truncation first though
static inline std::array utf8_malformed_c1{char8_t{0b11000001}};

// Lead byte, no subsequent byte.
static inline std::array utf8_trunc_2b_1b{char8_t{0b1100'0010}};

// One byte of a three byte sequence. Also would decode to zero which
// isn't legal but doesn't get that far
static inline std::array utf8_trunc_3b_1b{char8_t{0b1110'0000}};

// Two bytes of a three byte sequence. Also would decode to zero which
// isn't legal but doesn't get that far
static inline std::array utf8_trunc_3b_2b{char8_t{0b1110'0000}, char8_t{0b1000'0000}};


// One, two and three byte truncations of what look to be
// four byte sequences
static inline std::array utf8_trunc_4b_1b{char8_t{0b1111'0000}};
static inline std::array utf8_trunc_4b_2b{char8_t{0b1111'0000}, char8_t{0b1000'0000}};
static inline std::array utf8_trunc_4b_3b{char8_t{0b1111'0000},
                                          char8_t{0b1000'0000},
                                          char8_t{0b1000'0000}};

// Non-shortest encodings
static inline std::array utf8_nonshortest_2b_1{char8_t{0b1100'0000}, char8_t{0b1000'0000}};

static inline std::array utf8_nonshortest_3b_1{char8_t{0b1110'0000},
                                               char8_t{0b1000'0000},
                                               char8_t{0b1000'0000}};

static inline std::array utf8_nonshortest_4b_1{char8_t{0b1111'0000},
                                               char8_t{0b1000'0000},
                                               char8_t{0b1000'0000},
                                               char8_t{0b1000'0000}};
