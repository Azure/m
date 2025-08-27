// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace m::test_data
{
    using namespace std::string_literals;
    using namespace std::string_view_literals;

    // char
    auto nato_alphabet_s = {"Alfa"s,   "Bravo"s,    "Charlie"s, "Delta"s,   "Echo"s,    "Foxtrot"s,
                            "Golf"s,   "Hotel"s,    "India"s,   "Juliett"s, "Kilo"s,    "Lima"s,
                            "Mike"s,   "November"s, "Oscar"s,   "Papa"s,    "Quebec"s,  "Romeo"s,
                            "Sierra"s, "Tango"s,    "Uniform"s, "Victor"s,  "Whiskey"s, "Xray"s,
                            "Yankee"s, "Zulu"s};

    auto nato_alphabet_sv = {
        "Alfa"sv,   "Bravo"sv,   "Charlie"sv, "Delta"sv,  "Echo"sv,   "Foxtrot"sv, "Golf"sv,
        "Hotel"sv,  "India"sv,   "Juliett"sv, "Kilo"sv,   "Lima"sv,   "Mike"sv,    "November"sv,
        "Oscar"sv,  "Papa"sv,    "Quebec"sv,  "Romeo"sv,  "Sierra"sv, "Tango"sv,   "Uniform"sv,
        "Victor"sv, "Whiskey"sv, "Xray"sv,    "Yankee"sv, "Zulu"sv};

    // wchar_t
    auto nato_alphabet_ws = {
        L"Alfa"s,   L"Bravo"s,   L"Charlie"s, L"Delta"s,  L"Echo"s,   L"Foxtrot"s, L"Golf"s,
        L"Hotel"s,  L"India"s,   L"Juliett"s, L"Kilo"s,   L"Lima"s,   L"Mike"s,    L"November"s,
        L"Oscar"s,  L"Papa"s,    L"Quebec"s,  L"Romeo"s,  L"Sierra"s, L"Tango"s,   L"Uniform"s,
        L"Victor"s, L"Whiskey"s, L"Xray"s,    L"Yankee"s, L"Zulu"s};

    auto nato_alphabet_wsv = {L"Alfa"sv,    L"Bravo"sv,  L"Charlie"sv, L"Delta"sv,    L"Echo"sv,
                              L"Foxtrot"sv, L"Golf"sv,   L"Hotel"sv,   L"India"sv,    L"Juliett"sv,
                              L"Kilo"sv,    L"Lima"sv,   L"Mike"sv,    L"November"sv, L"Oscar"sv,
                              L"Papa"sv,    L"Quebec"sv, L"Romeo"sv,   L"Sierra"sv,   L"Tango"sv,
                              L"Uniform"sv, L"Victor"sv, L"Whiskey"sv, L"Xray"sv,     L"Yankee"sv,
                              L"Zulu"sv};

    // char8_t
    auto nato_alphabet_u8s = {u8"Alfa"s,    u8"Bravo"s,  u8"Charlie"s, u8"Delta"s,    u8"Echo"s,
                              u8"Foxtrot"s, u8"Golf"s,   u8"Hotel"s,   u8"India"s,    u8"Juliett"s,
                              u8"Kilo"s,    u8"Lima"s,   u8"Mike"s,    u8"November"s, u8"Oscar"s,
                              u8"Papa"s,    u8"Quebec"s, u8"Romeo"s,   u8"Sierra"s,   u8"Tango"s,
                              u8"Uniform"s, u8"Victor"s, u8"Whiskey"s, u8"Xray"s,     u8"Yankee"s,
                              u8"Zulu"s};

    auto nato_alphabet_u8sv = {
        u8"Alfa"sv,   u8"Bravo"sv,    u8"Charlie"sv, u8"Delta"sv,   u8"Echo"sv,    u8"Foxtrot"sv,
        u8"Golf"sv,   u8"Hotel"sv,    u8"India"sv,   u8"Juliett"sv, u8"Kilo"sv,    u8"Lima"sv,
        u8"Mike"sv,   u8"November"sv, u8"Oscar"sv,   u8"Papa"sv,    u8"Quebec"sv,  u8"Romeo"sv,
        u8"Sierra"sv, u8"Tango"sv,    u8"Uniform"sv, u8"Victor"sv,  u8"Whiskey"sv, u8"Xray"sv,
        u8"Yankee"sv, u8"Zulu"sv};

    // char16_t
    auto nato_alphabet_us = {
        u"Alfa"s,   u"Bravo"s,   u"Charlie"s, u"Delta"s,  u"Echo"s,   u"Foxtrot"s, u"Golf"s,
        u"Hotel"s,  u"India"s,   u"Juliett"s, u"Kilo"s,   u"Lima"s,   u"Mike"s,    u"November"s,
        u"Oscar"s,  u"Papa"s,    u"Quebec"s,  u"Romeo"s,  u"Sierra"s, u"Tango"s,   u"Uniform"s,
        u"Victor"s, u"Whiskey"s, u"Xray"s,    u"Yankee"s, u"Zulu"s};

    auto nato_alphabet_usv = {u"Alfa"sv,    u"Bravo"sv,  u"Charlie"sv, u"Delta"sv,    u"Echo"sv,
                              u"Foxtrot"sv, u"Golf"sv,   u"Hotel"sv,   u"India"sv,    u"Juliett"sv,
                              u"Kilo"sv,    u"Lima"sv,   u"Mike"sv,    u"November"sv, u"Oscar"sv,
                              u"Papa"sv,    u"Quebec"sv, u"Romeo"sv,   u"Sierra"sv,   u"Tango"sv,
                              u"Uniform"sv, u"Victor"sv, u"Whiskey"sv, u"Xray"sv,     u"Yankee"sv,
                              u"Zulu"sv};

    // char32_t
    auto nato_alphabet_u32s = {
        U"Alfa"s,   U"Bravo"s,   U"Charlie"s, U"Delta"s,  U"Echo"s,   U"Foxtrot"s, U"Golf"s,
        U"Hotel"s,  U"India"s,   U"Juliett"s, U"Kilo"s,   U"Lima"s,   U"Mike"s,    U"November"s,
        U"Oscar"s,  U"Papa"s,    U"Quebec"s,  U"Romeo"s,  U"Sierra"s, U"Tango"s,   U"Uniform"s,
        U"Victor"s, U"Whiskey"s, U"Xray"s,    U"Yankee"s, U"Zulu"s};

    auto nato_alphabet_u32sv = {
        U"Alfa"sv,   U"Bravo"sv,    U"Charlie"sv, U"Delta"sv,   U"Echo"sv,    U"Foxtrot"sv,
        U"Golf"sv,   U"Hotel"sv,    U"India"sv,   U"Juliett"sv, U"Kilo"sv,    U"Lima"sv,
        U"Mike"sv,   U"November"sv, U"Oscar"sv,   U"Papa"sv,    U"Quebec"sv,  U"Romeo"sv,
        U"Sierra"sv, U"Tango"sv,    U"Uniform"sv, U"Victor"sv,  U"Whiskey"sv, U"Xray"sv,
        U"Yankee"sv, U"Zulu"sv};

    namespace utf_data
    {
        struct data_set
        {
            data_set(std::initializer_list<char8_t>  utf8data,
                     std::initializer_list<char16_t> utf16data,
                     std::initializer_list<char32_t> utf32data):
                m_u8_chardata(utf8data.begin(), utf8data.end()),
                m_u16le_chardata(utf16data.begin(), utf16data.end()),
                m_u32_chardata(utf32data.begin(), utf32data.end())
            {
                std::ranges::transform(m_u16le_chardata,
                                       std::back_inserter(m_u16be_chardata),
                                       [](char16_t ch) -> char16_t {
                                           return ((ch & 0xff) << 8) | ((ch >> 8) & 0xff);
                                       });

                m_u8_sv    = std::u8string_view(m_u8_chardata.begin(), m_u8_chardata.end());
                m_u16le_sv = std::u16string_view(m_u16le_chardata.begin(), m_u16le_chardata.end());
                m_u16be_sv = std::u16string_view(m_u16be_chardata.begin(), m_u16be_chardata.end());
                m_u32_sv   = std::u32string_view(m_u32_chardata.begin(), m_u32_chardata.end());

                m_u8_byte_data =
                    std::as_bytes(std::span(m_u8_chardata.begin(), m_u8_chardata.end()));
                m_u16le_byte_data =
                    std::as_bytes(std::span(m_u16le_chardata.begin(), m_u16le_chardata.end()));
                m_u16be_byte_data =
                    std::as_bytes(std::span(m_u16be_chardata.begin(), m_u16be_chardata.end()));
                m_u32_byte_data =
                    std::as_bytes(std::span(m_u32_chardata.begin(), m_u32_chardata.end()));
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
        auto const empty_data = data_set{{}, {}, {}};
        // static inline data_set empty_data({}, {}, {});

        static inline data_set
            hellodata({char8_t{'h'}, char8_t{'e'}, char8_t{'l'}, char8_t{'l'}, char8_t{'o'}},
                      {char16_t{'h'}, char16_t{'e'}, char16_t{'l'}, char16_t{'l'}, char16_t{'o'}},
                      {char32_t{'h'}, char32_t{'e'}, char32_t{'l'}, char32_t{'l'}, char32_t{'o'}});

        // The character sequence U+0041 U+2262 U+0391 U+002E "A<NOT IDENTICAL
        //    TO><ALPHA>." is encoded in UTF-8 as follows:

        static inline data_set
            rfc3629_ex_1({char8_t{0x41},
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

        static inline data_set rfc3629_ex_2({0xed, 0x95, 0x9c, 0xea, 0xb5, 0xad, 0xec, 0x96, 0xb4},
                                            {0xd55c, 0xad6d, 0xc5b4},
                                            {0xd55c, 0xad6d, 0xc5b4});

        // The character sequence U+65E5 U+672C U+8A9E (Japanese "nihongo",
        // meaning "the Japanese language") is encoded in UTF-8 as follows:
        //
        //    --------+--------+--------
        //    E6 97 A5 E6 9C AC E8 AA 9E
        //    --------+--------+--------

        static inline data_set rfc3629_ex_3({0xe6, 0x97, 0xa5, 0xe6, 0x9c, 0xac, 0xe8, 0xaa, 0x9e},
                                            {0x65e5, 0x672c, 0x8a9e},
                                            {0x65e5, 0x672c, 0x8a9e});

        // The character U+233B4 (a Chinese character meaning 'stump of tree'),
        // prepended with a UTF-8 BOM, is encoded in UTF-8 as follows:
        //
        //    --------+-----------
        //    EF BB BF F0 A3 8E B4
        //    --------+-----------

        static inline data_set rfc3629_ex_4({0xef, 0xbb, 0xbf, 0xf0, 0xa3, 0x8e, 0xb4},
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
    } // namespace utf_data

} // namespace m::test_data
