// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <array>
#include <initializer_list>
#include <string_view>
#include <vector>

#include <m/multi_byte/code_page.h>

using namespace std::string_view_literals;

using test_uchar = unsigned char;

struct mb_test_data
{
    mb_test_data(m::multi_byte::code_page             cp,
                 std::initializer_list<test_uchar> chars,
                 std::wstring_view                    view):
        m_cp(cp), m_wview(view)
    {
        // char may be signed so the bytes are stored as test_uchar, but we
        // want a vector of char to work with the APIs.
        //
        std::ranges::transform(chars, std::back_inserter(m_chars), [](test_uchar ch) -> char {
            return static_cast<char>(ch);
        });

        m_view = std::string_view(m_chars.begin(), m_chars.end());
        m_u16view =
            std::u16string_view(reinterpret_cast<char16_t const*>(m_wview.data()), m_wview.size());
    }

    m::multi_byte::code_page m_cp;
    std::vector<char>        m_chars;
    std::string_view         m_view;
    std::wstring_view        m_wview;
    std::u16string_view      m_u16view;
};

// "Big5" data. Making it up, it's almost certainly non-sensical

//
// From: https://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/WINDOWS/CP950.TXT
//
// 0xA24E	0xFE6B	#SMALL COMMERCIAL AT
// 0xA24F	0x33D5	#SQUARE MIL
// 0xA250	0x339C	#SQUARE MM
// 0xA251	0x339D	#SQUARE CM
// 0xA252	0x339E	#SQUARE KM
// 0xA253	0x33CE	#SQUARE KM CAPITAL
// 0xA254	0x33A1	#SQUARE M SQUARED
// 0xA255	0x338E	#SQUARE MG
// 0xA256	0x338F	#SQUARE KG
// 0xA257	0x33C4	#SQUARE CC
// 0xA258	0x00B0	#DEGREE SIGN
// 0xA259	0x5159	#CJK UNIFIED IDEOGRAPH
// 0xA25A	0x515B	#CJK UNIFIED IDEOGRAPH
// 0xA25B	0x515E	#CJK UNIFIED IDEOGRAPH
// 0xA25C	0x515D	#CJK UNIFIED IDEOGRAPH
// 0xA25D	0x5161	#CJK UNIFIED IDEOGRAPH
// 0xA25E	0x5163	#CJK UNIFIED IDEOGRAPH
// 0xA25F	0x55E7	#CJK UNIFIED IDEOGRAPH
// 0xA260	0x74E9	#CJK UNIFIED IDEOGRAPH
// 0xA261	0x7CCE	#CJK UNIFIED IDEOGRAPH
// 0xA262	0x2581	#LOWER ONE EIGHTH BLOCK
// 0xA263	0x2582	#LOWER ONE QUARTER BLOCK
// 0xA264	0x2583	#LOWER THREE EIGHTHS BLOCK
// 0xA265	0x2584	#LOWER HALF BLOCK
// 0xA266	0x2585	#LOWER FIVE EIGHTHS BLOCK
// 0xA267	0x2586	#LOWER THREE QUARTERS BLOCK
// 0xA268	0x2587	#LOWER SEVEN EIGHTHS BLOCK
// 0xA269	0x2588	#FULL BLOCK
// 0xA26A	0x258F	#LEFT ONE EIGHTH BLOCK
// 0xA26B	0x258E	#LEFT ONE QUARTER BLOCK
// 0xA26C	0x258D	#LEFT THREE EIGHTHS BLOCK
// 0xA26D	0x258C	#LEFT HALF BLOCK
// 0xA26E	0x258B	#LEFT FIVE EIGHTHS BLOCK
// 0xA26F	0x258A	#LEFT THREE QUARTERS BLOCK
// 0xA270	0x2589	#LEFT SEVEN EIGHTHS BLOCK
// 0xA271	0x253C	#BOX DRAWINGS LIGHT VERTICAL AND HORIZONTAL
// 0xA272	0x2534	#BOX DRAWINGS LIGHT UP AND HORIZONTAL
// 0xA273	0x252C	#BOX DRAWINGS LIGHT DOWN AND HORIZONTAL
// 0xA274	0x2524	#BOX DRAWINGS LIGHT VERTICAL AND LEFT
// 0xA275	0x251C	#BOX DRAWINGS LIGHT VERTICAL AND RIGHT
// 0xA276	0x2594	#UPPER ONE EIGHTH BLOCK
// 0xA277	0x2500	#BOX DRAWINGS LIGHT HORIZONTAL
// 0xA278	0x2502	#BOX DRAWINGS LIGHT VERTICAL
// 0xA279	0x2595	#RIGHT ONE EIGHTH BLOCK
// 0xA27A	0x250C	#BOX DRAWINGS LIGHT DOWN AND RIGHT
// 0xA27B	0x2510	#BOX DRAWINGS LIGHT DOWN AND LEFT
// 0xA27C	0x2514	#BOX DRAWINGS LIGHT UP AND RIGHT
//

// # SMALL COMMERCIAL AT
inline auto mb_cp950_t1 = mb_test_data(m::multi_byte::code_page{950},
                                       {
                                           test_uchar(0xa2),
                                           test_uchar{0x4e},
                                       },
                                       L"\ufe6b"sv);

// # SMALL COMMERCIAL AT, #SQUARE MIL, U+5159, #FULL BLOCK
inline auto mb_cp950_t2 = mb_test_data(m::multi_byte::code_page{950},
                                       {
                                           test_uchar(0xa2),
                                           test_uchar{0x4e},
                                           test_uchar(0xa2),
                                           test_uchar{0x4f},
                                           test_uchar(0xa2),
                                           test_uchar{0x59},
                                           test_uchar(0xa2),
                                           test_uchar{0x69},
                                       },
                                       L"\ufe6b\u33d5\u5159\u2588"sv);

//
// How large of a data set do we need and how "representative" does it need
// to be?
//
// we are not testing windows code pages. In fact, exercising more code pages
// will only make the tests more fragile since we do not know what code pages
// are installed on the majority of machines other than the boring
// Windows-1252 and it seems like 950 which is at least interesting is also
// commonly installed.
//
// The point of this exercise is to drive the multi_byte APIs and their buffer
// handling.
//
// So we will take the data from above, and replicate it to a large number
// of times.
//

// # SMALL COMMERCIAL AT, #SQUARE MIL, U+5159, #FULL BLOCK
// four times over
inline auto mb_cp950_t3 = mb_test_data(
    m::multi_byte::code_page{950},
    {
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
    },
    L"\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588"sv);

// # SMALL COMMERCIAL AT, #SQUARE MIL, U+5159, #FULL BLOCK
// 16 times over
inline auto mb_cp950_t4 = mb_test_data(
    m::multi_byte::code_page{950},
    {
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
    },
    L"\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588"
    L"\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588"
    L"\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588"
    L"\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588"sv);

// # SMALL COMMERCIAL AT, #SQUARE MIL, U+5159, #FULL BLOCK
// 64 times over
inline auto mb_cp950_t5 = mb_test_data(
    m::multi_byte::code_page{950},
    {
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
        test_uchar(0xa2), test_uchar{0x4e}, test_uchar(0xa2), test_uchar{0x4f},
        test_uchar(0xa2), test_uchar{0x59}, test_uchar(0xa2), test_uchar{0x69},
    },
    L"\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588"
    L"\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588"
    L"\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588"
    L"\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588"
    L"\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588"
    L"\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588"
    L"\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588"
    L"\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588"
    L"\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588"
    L"\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588"
    L"\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588"
    L"\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588"
    L"\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588"
    L"\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588"
    L"\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588"
    L"\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588\ufe6b\u33d5\u5159\u2588"sv);

//
// In order to validate that the partial decode scenarios are working correctly,
// we need to have non-repeating data. so here goes copying just the table from
// the Unicode site and the code points in sequence. The internal buffer size is
// 128 by default but we can adjust it down. In order to have torn characters we
// will have arbitrary single byte characters, "-" added.
//

// 0xA179	0x300E	#LEFT WHITE CORNER BRACKET
// 0xA17A	0x300F	#RIGHT WHITE CORNER BRACKET
// 0xA17B	0xFE43	#PRESENTATION FORM FOR VERTICAL LEFT WHITE CORNER BRACKET
// 0xA17C	0xFE44	#PRESENTATION FORM FOR VERTICAL RIGHT WHITE CORNER BRACKET
// 0xA17D	0xFE59	#SMALL LEFT PARENTHESIS
// 0xA17E	0xFE5A	#SMALL RIGHT PARENTHESIS
// 0xA1A1	0xFE5B	#SMALL LEFT CURLY BRACKET
// 0xA1A2	0xFE5C	#SMALL RIGHT CURLY BRACKET
// 0xA1A3	0xFE5D	#SMALL LEFT TORTOISE SHELL BRACKET
// 0xA1A4	0xFE5E	#SMALL RIGHT TORTOISE SHELL BRACKET
// 0xA1A5	0x2018	#LEFT SINGLE QUOTATION MARK
// 0xA1A6	0x2019	#RIGHT SINGLE QUOTATION MARK
// 0xA1A7	0x201C	#LEFT DOUBLE QUOTATION MARK
// 0xA1A8	0x201D	#RIGHT DOUBLE QUOTATION MARK
// 0xA1A9	0x301D	#REVERSED DOUBLE PRIME QUOTATION MARK
// 0xA1AA	0x301E	#DOUBLE PRIME QUOTATION MARK
// 0xA1AB	0x2035	#REVERSED PRIME
// 0xA1AC	0x2032	#PRIME
// 0xA1AD	0xFF03	#FULLWIDTH NUMBER SIGN
// 0xA1AE	0xFF06	#FULLWIDTH AMPERSAND
// 0xA1AF	0xFF0A	#FULLWIDTH ASTERISK
// 0xA1B0	0x203B	#REFERENCE MARK
// 0xA1B1	0x00A7	#SECTION SIGN
// 0xA1B2	0x3003	#DITTO MARK
// 0xA1B3	0x25CB	#WHITE CIRCLE
// 0xA1B4	0x25CF	#BLACK CIRCLE
// 0xA1B5	0x25B3	#WHITE UP-POINTING TRIANGLE
// 0xA1B6	0x25B2	#BLACK UP-POINTING TRIANGLE
// 0xA1B7	0x25CE	#BULLSEYE
// 0xA1B8	0x2606	#WHITE STAR
// 0xA1B9	0x2605	#BLACK STAR
// 0xA1BA	0x25C7	#WHITE DIAMOND
// 0xA1BB	0x25C6	#BLACK DIAMOND
// 0xA1BC	0x25A1	#WHITE SQUARE
// 0xA1BD	0x25A0	#BLACK SQUARE
// 0xA1BE	0x25BD	#WHITE DOWN-POINTING TRIANGLE
// 0xA1BF	0x25BC	#BLACK DOWN-POINTING TRIANGLE
// 0xA1C0	0x32A3	#CIRCLED IDEOGRAPH CORRECT
// 0xA1C1	0x2105	#CARE OF
// 0xA1C2	0x00AF	#MACRON
// 0xA1C3	0xFFE3	#FULLWIDTH MACRON
// 0xA1C4	0xFF3F	#FULLWIDTH LOW LINE
// 0xA1C5	0x02CD	#MODIFIER LETTER LOW MACRON
// 0xA1C6	0xFE49	#DASHED OVERLINE
// 0xA1C7	0xFE4A	#CENTRELINE OVERLINE
// 0xA1C8	0xFE4D	#DASHED LOW LINE
// 0xA1C9	0xFE4E	#CENTRELINE LOW LINE
// 0xA1CA	0xFE4B	#WAVY OVERLINE
// 0xA1CB	0xFE4C	#DOUBLE WAVY OVERLINE
// 0xA1CC	0xFE5F	#SMALL NUMBER SIGN
// 0xA1CD	0xFE60	#SMALL AMPERSAND
// 0xA1CE	0xFE61	#SMALL ASTERISK
// 0xA1CF	0xFF0B	#FULLWIDTH PLUS SIGN
// 0xA1D0	0xFF0D	#FULLWIDTH HYPHEN-MINUS
// 0xA1D1	0x00D7	#MULTIPLICATION SIGN
// 0xA1D2	0x00F7	#DIVISION SIGN
// 0xA1D3	0x00B1	#PLUS-MINUS SIGN
// 0xA1D4	0x221A	#SQUARE ROOT
// 0xA1D5	0xFF1C	#FULLWIDTH LESS-THAN SIGN
// 0xA1D6	0xFF1E	#FULLWIDTH GREATER-THAN SIGN
// 0xA1D7	0xFF1D	#FULLWIDTH EQUALS SIGN
// 0xA1D8	0x2266	#LESS-THAN OVER EQUAL TO
// 0xA1D9	0x2267	#GREATER-THAN OVER EQUAL TO
// 0xA1DA	0x2260	#NOT EQUAL TO
// 0xA1DB	0x221E	#INFINITY
// 0xA1DC	0x2252	#APPROXIMATELY EQUAL TO OR THE IMAGE OF
// 0xA1DD	0x2261	#IDENTICAL TO
// 0xA1DE	0xFE62	#SMALL PLUS SIGN
// 0xA1DF	0xFE63	#SMALL HYPHEN-MINUS
// 0xA1E0	0xFE64	#SMALL LESS-THAN SIGN
// 0xA1E1	0xFE65	#SMALL GREATER-THAN SIGN
// 0xA1E2	0xFE66	#SMALL EQUALS SIGN
// 0xA1E3	0xFF5E	#FULLWIDTH TILDE
// 0xA1E4	0x2229	#INTERSECTION
// 0xA1E5	0x222A	#UNION
// 0xA1E6	0x22A5	#UP TACK
// 0xA1E7	0x2220	#ANGLE
// 0xA1E8	0x221F	#RIGHT ANGLE
// 0xA1E9	0x22BF	#RIGHT TRIANGLE
// 0xA1EA	0x33D2	#SQUARE LOG
// 0xA1EB	0x33D1	#SQUARE LN
// 0xA1EC	0x222B	#INTEGRAL
// 0xA1ED	0x222E	#CONTOUR INTEGRAL
// 0xA1EE	0x2235	#BECAUSE
// 0xA1EF	0x2234	#THEREFORE
// 0xA1F0	0x2640	#FEMALE SIGN
// 0xA1F1	0x2642	#MALE SIGN
// 0xA1F2	0x2295	#CIRCLED PLUS
// 0xA1F3	0x2299	#CIRCLED DOT OPERATOR
// 0xA1F4	0x2191	#UPWARDS ARROW
// 0xA1F5	0x2193	#DOWNWARDS ARROW
// 0xA1F6	0x2190	#LEFTWARDS ARROW
// 0xA1F7	0x2192	#RIGHTWARDS ARROW
// 0xA1F8	0x2196	#NORTH WEST ARROW
// 0xA1F9	0x2197	#NORTH EAST ARROW
// 0xA1FA	0x2199	#SOUTH WEST ARROW
// 0xA1FB	0x2198	#SOUTH EAST ARROW
// 0xA1FC	0x2225	#PARALLEL TO
// 0xA1FD	0x2223	#DIVIDES
// 0xA1FE	0xFF0F	#FULLWIDTH SOLIDUS
// 0xA240	0xFF3C	#FULLWIDTH REVERSE SOLIDUS
// 0xA241	0x2215	#DIVISION SLASH
// 0xA242	0xFE68	#SMALL REVERSE SOLIDUS

inline auto mb_cp950_t6 =
    mb_test_data(m::multi_byte::code_page{950},
                 {
                     //
                     // 0xA179	0x300E	#LEFT WHITE CORNER BRACKET
                     test_uchar(0xA1),
                     test_uchar(0x79),
                     //
                     // 0xA17A	0x300F	#RIGHT WHITE CORNER BRACKET
                     test_uchar(0xA1),
                     test_uchar(0x7A),
                     //
                     // 0xA17B	0xFE43	#PRESENTATION FORM FOR VERTICAL LEFT WHITE CORNER BRACKET
                     test_uchar(0xA1),
                     test_uchar(0x7B),
                     //
                     // 0xA17C	0xFE44	#PRESENTATION FORM FOR VERTICAL RIGHT WHITE CORNER BRACKET
                     test_uchar(0xA1),
                     test_uchar(0x7C),
                     //
                     // 0xA17D	0xFE59	#SMALL LEFT PARENTHESIS
                     test_uchar(0xA1),
                     test_uchar(0x7D),
                     //
                     // 0xA17E	0xFE5A	#SMALL RIGHT PARENTHESIS
                     test_uchar(0xA1),
                     test_uchar(0x7E),
                     //
                     // 0xA1A1	0xFE5B	#SMALL LEFT CURLY BRACKET
                     test_uchar(0xA1),
                     test_uchar(0xA1),
                     //
                     // 0xA1A2	0xFE5C	#SMALL RIGHT CURLY BRACKET
                     test_uchar(0xA1),
                     test_uchar(0xA2),
                     //
                     // 0xA1A3	0xFE5D	#SMALL LEFT TORTOISE SHELL BRACKET
                     test_uchar(0xA1),
                     test_uchar(0xA3),
                     //
                     // 0xA1A4	0xFE5E	#SMALL RIGHT TORTOISE SHELL BRACKET
                     test_uchar(0xA1),
                     test_uchar(0xA4),
                     //
                     // 0xA1A5	0x2018	#LEFT SINGLE QUOTATION MARK
                     test_uchar(0xA1),
                     test_uchar(0xA5),
                     //
                     // 0xA1A6	0x2019	#RIGHT SINGLE QUOTATION MARK
                     test_uchar(0xA1),
                     test_uchar(0xA6),
                     //
                     // 0xA1A7	0x201C	#LEFT DOUBLE QUOTATION MARK
                     test_uchar(0xA1),
                     test_uchar(0xA7),
                     //
                     // 0xA1A8	0x201D	#RIGHT DOUBLE QUOTATION MARK
                     test_uchar(0xA1),
                     test_uchar(0xA8),
                     //
                     // 0xA1A9	0x301D	#REVERSED DOUBLE PRIME QUOTATION MARK
                     test_uchar(0xA1),
                     test_uchar(0xA9),
                     //
                     // 0xA1AA	0x301E	#DOUBLE PRIME QUOTATION MARK
                     test_uchar(0xA1),
                     test_uchar(0xAA),
                     //
                     // 0xA1AB	0x2035	#REVERSED PRIME
                     test_uchar(0xA1),
                     test_uchar(0xAB),
                     //
                     // 0xA1AC	0x2032	#PRIME
                     test_uchar(0xA1),
                     test_uchar(0xAC),
                     //
                     // 0xA1AD	0xFF03	#FULLWIDTH NUMBER SIGN
                     test_uchar(0xA1),
                     test_uchar(0xAD),
                     //
                     // 0xA1AE	0xFF06	#FULLWIDTH AMPERSAND
                     test_uchar(0xA1),
                     test_uchar(0xAE),
                     //
                     // 0xA1AF	0xFF0A	#FULLWIDTH ASTERISK
                     test_uchar(0xA1),
                     test_uchar(0xAF),
                     //
                     // 0xA1B0	0x203B	#REFERENCE MARK
                     test_uchar(0xA1),
                     test_uchar(0xB0),
                     //
                     // 0xA1B1	0x00A7	#SECTION SIGN
                     test_uchar(0xA1),
                     test_uchar(0xB1),
                     //
                     // 0xA1B2	0x3003	#DITTO MARK
                     test_uchar(0xA1),
                     test_uchar(0xB2),
                     //
                     // 0xA1B3	0x25CB	#WHITE CIRCLE
                     test_uchar(0xA1),
                     test_uchar(0xB3),
                     //
                     // 0xA1B4	0x25CF	#BLACK CIRCLE
                     test_uchar(0xA1),
                     test_uchar(0xB4),
                     //
                     // 0xA1B5	0x25B3	#WHITE UP-POINTING TRIANGLE
                     test_uchar(0xA1),
                     test_uchar(0xB5),
                     //
                     // 0xA1B6	0x25B2	#BLACK UP-POINTING TRIANGLE
                     test_uchar(0xA1),
                     test_uchar(0xB6),
                     //
                     // 0xA1B7	0x25CE	#BULLSEYE
                     test_uchar(0xA1),
                     test_uchar(0xB7),
                     //
                     // 0xA1B8	0x2606	#WHITE STAR
                     test_uchar(0xA1),
                     test_uchar(0xB8),
                     //
                     // 0xA1B9	0x2605	#BLACK STAR
                     test_uchar(0xA1),
                     test_uchar(0xB9),
                     //
                     // 0xA1BA	0x25C7	#WHITE DIAMOND
                     test_uchar(0xA1),
                     test_uchar(0xBA),
                     //
                     // 0xA1BB	0x25C6	#BLACK DIAMOND
                     test_uchar(0xA1),
                     test_uchar(0xBB),
                     //
                     // 0xA1BC	0x25A1	#WHITE SQUARE
                     test_uchar(0xA1),
                     test_uchar(0xBC),
                     //
                     // 0xA1BD	0x25A0	#BLACK SQUARE
                     test_uchar(0xA1),
                     test_uchar(0xBD),
                     //
                     // 0xA1BE	0x25BD	#WHITE DOWN-POINTING TRIANGLE
                     test_uchar(0xA1),
                     test_uchar(0xBE),
                     //
                     // 0xA1BF	0x25BC	#BLACK DOWN-POINTING TRIANGLE
                     test_uchar(0xA1),
                     test_uchar(0xBF),
                     //
                     // 0xA1C0	0x32A3	#CIRCLED IDEOGRAPH CORRECT
                     test_uchar(0xA1),
                     test_uchar(0xC0),
                     //
                     // 0xA1C1	0x2105	#CARE OF
                     test_uchar(0xA1),
                     test_uchar(0xC1),
                     //
                     // 0xA1C2	0x00AF	#MACRON
                     test_uchar(0xA1),
                     test_uchar(0xC2),
                     //
                     // 0xA1C3	0xFFE3	#FULLWIDTH MACRON
                     test_uchar(0xA1),
                     test_uchar(0xC3),
                     //
                     // 0xA1C4	0xFF3F	#FULLWIDTH LOW LINE
                     test_uchar(0xA1),
                     test_uchar(0xC4),
                     //
                     // 0xA1C5	0x02CD	#MODIFIER LETTER LOW MACRON
                     test_uchar(0xA1),
                     test_uchar(0xC5),
                     //
                     // 0xA1C6	0xFE49	#DASHED OVERLINE
                     test_uchar(0xA1),
                     test_uchar(0xC6),
                     //
                     // 0xA1C7	0xFE4A	#CENTRELINE OVERLINE
                     test_uchar(0xA1),
                     test_uchar(0xC7),
                     //
                     // 0xA1C8	0xFE4D	#DASHED LOW LINE
                     test_uchar(0xA1),
                     test_uchar(0xC8),
                     //
                     // 0xA1C9	0xFE4E	#CENTRELINE LOW LINE
                     test_uchar(0xA1),
                     test_uchar(0xC9),
                     //
                     // 0xA1CA	0xFE4B	#WAVY OVERLINE
                     test_uchar(0xA1),
                     test_uchar(0xCA),
                     //
                     // 0xA1CB	0xFE4C	#DOUBLE WAVY OVERLINE
                     test_uchar(0xA1),
                     test_uchar(0xCB),
                     //
                     // 0xA1CC	0xFE5F	#SMALL NUMBER SIGN
                     test_uchar(0xA1),
                     test_uchar(0xCC),
                     //
                     // 0xA1CD	0xFE60	#SMALL AMPERSAND
                     test_uchar(0xA1),
                     test_uchar(0xCD),
                     //
                     // 0xA1CE	0xFE61	#SMALL ASTERISK
                     test_uchar(0xA1),
                     test_uchar(0xCE),
                     //
                     // 0xA1CF	0xFF0B	#FULLWIDTH PLUS SIGN
                     test_uchar(0xA1),
                     test_uchar(0xCF),
                     //
                     // 0xA1D0	0xFF0D	#FULLWIDTH HYPHEN-MINUS
                     test_uchar(0xA1),
                     test_uchar(0xD0),
                     //
                     // 0xA1D1	0x00D7	#MULTIPLICATION SIGN
                     test_uchar(0xA1),
                     test_uchar(0xD1),
                     //
                     // 0xA1D2	0x00F7	#DIVISION SIGN
                     test_uchar(0xA1),
                     test_uchar(0xD2),
                     //
                     // 0xA1D3	0x00B1	#PLUS-MINUS SIGN
                     test_uchar(0xA1),
                     test_uchar(0xD3),
                     //
                     // 0xA1D4	0x221A	#SQUARE ROOT
                     test_uchar(0xA1),
                     test_uchar(0xD4),
                     //
                     // 0xA1D5	0xFF1C	#FULLWIDTH LESS-THAN SIGN
                     test_uchar(0xA1),
                     test_uchar(0xD5),
                     //
                     // 0xA1D6	0xFF1E	#FULLWIDTH GREATER-THAN SIGN
                     test_uchar(0xA1),
                     test_uchar(0xD6),
                     //
                     // 0xA1D7	0xFF1D	#FULLWIDTH EQUALS SIGN
                     test_uchar(0xA1),
                     test_uchar(0xD7),
                     //
                     // 0xA1D8	0x2266	#LESS-THAN OVER EQUAL TO
                     test_uchar(0xA1),
                     test_uchar(0xD8),
                     //
                     // 0xA1D9	0x2267	#GREATER-THAN OVER EQUAL TO
                     test_uchar(0xA1),
                     test_uchar(0xD9),
                     //
                     // 0xA1DA	0x2260	#NOT EQUAL TO
                     test_uchar(0xA1),
                     test_uchar(0xDA),
                     //
                     // 0xA1DB	0x221E	#INFINITY
                     test_uchar(0xA1),
                     test_uchar(0xDB),
                     //
                     // 0xA1DC	0x2252	#APPROXIMATELY EQUAL TO OR THE IMAGE OF
                     test_uchar(0xA1),
                     test_uchar(0xDC),
                     //
                     // 0xA1DD	0x2261	#IDENTICAL TO
                     test_uchar(0xA1),
                     test_uchar(0xDD),
                     //
                     // 0xA1DE	0xFE62	#SMALL PLUS SIGN
                     test_uchar(0xA1),
                     test_uchar(0xDE),
                     //
                     // 0xA1DF	0xFE63	#SMALL HYPHEN-MINUS
                     test_uchar(0xA1),
                     test_uchar(0xDF),
                     //
                     // 0xA1E0	0xFE64	#SMALL LESS-THAN SIGN
                     test_uchar(0xA1),
                     test_uchar(0xE0),
                     //
                     // 0xA1E1	0xFE65	#SMALL GREATER-THAN SIGN
                     test_uchar(0xA1),
                     test_uchar(0xE1),
                     //
                     // 0xA1E2	0xFE66	#SMALL EQUALS SIGN
                     test_uchar(0xA1),
                     test_uchar(0xE2),
                     //
                     // 0xA1E3	0xFF5E	#FULLWIDTH TILDE
                     test_uchar(0xA1),
                     test_uchar(0xE3),
                     //
                     // 0xA1E4	0x2229	#INTERSECTION
                     test_uchar(0xA1),
                     test_uchar(0xE4),
                     //
                     // 0xA1E5	0x222A	#UNION
                     test_uchar(0xA1),
                     test_uchar(0xE5),
                     //
                     // 0xA1E6	0x22A5	#UP TACK
                     test_uchar(0xA1),
                     test_uchar(0xE6),
                     //
                     // 0xA1E7	0x2220	#ANGLE
                     test_uchar(0xA1),
                     test_uchar(0xE7),
                     //
                     // 0xA1E8	0x221F	#RIGHT ANGLE
                     test_uchar(0xA1),
                     test_uchar(0xE8),
                     //
                     // 0xA1E9	0x22BF	#RIGHT TRIANGLE
                     test_uchar(0xA1),
                     test_uchar(0xE9),
                     //
                     // 0xA1EA	0x33D2	#SQUARE LOG
                     test_uchar(0xA1),
                     test_uchar(0xEA),
                     //
                     // 0xA1EB	0x33D1	#SQUARE LN
                     test_uchar(0xA1),
                     test_uchar(0xEB),
                     //
                     // 0xA1EC	0x222B	#INTEGRAL
                     test_uchar(0xA1),
                     test_uchar(0xEC),
                     //
                     // 0xA1ED	0x222E	#CONTOUR INTEGRAL
                     test_uchar(0xA1),
                     test_uchar(0xED),
                     //
                     // 0xA1EE	0x2235	#BECAUSE
                     test_uchar(0xA1),
                     test_uchar(0xEE),
                     //
                     // 0xA1EF	0x2234	#THEREFORE
                     test_uchar(0xA1),
                     test_uchar(0xEF),
                     //
                     // 0xA1F0	0x2640	#FEMALE SIGN
                     test_uchar(0xA1),
                     test_uchar(0xF0),
                     //
                     // 0xA1F1	0x2642	#MALE SIGN
                     test_uchar(0xA1),
                     test_uchar(0xF1),
                     //
                     // 0xA1F2	0x2295	#CIRCLED PLUS
                     test_uchar(0xA1),
                     test_uchar(0xF2),
                     //
                     // 0xA1F3	0x2299	#CIRCLED DOT OPERATOR
                     test_uchar(0xA1),
                     test_uchar(0xF3),
                     //
                     // 0xA1F4	0x2191	#UPWARDS ARROW
                     test_uchar(0xA1),
                     test_uchar(0xF4),
                     //
                     // 0xA1F5	0x2193	#DOWNWARDS ARROW
                     test_uchar(0xA1),
                     test_uchar(0xF5),
                     //
                     // 0xA1F6	0x2190	#LEFTWARDS ARROW
                     test_uchar(0xA1),
                     test_uchar(0xF6),
                     //
                     // 0xA1F7	0x2192	#RIGHTWARDS ARROW
                     test_uchar(0xA1),
                     test_uchar(0xF7),
                     //
                     // 0xA1F8	0x2196	#NORTH WEST ARROW
                     test_uchar(0xA1),
                     test_uchar(0xF8),
                     //
                     // 0xA1F9	0x2197	#NORTH EAST ARROW
                     test_uchar(0xA1),
                     test_uchar(0xF9),
                     //
                     // 0xA1FA	0x2199	#SOUTH WEST ARROW
                     test_uchar(0xA1),
                     test_uchar(0xFA),
                     //
                     // 0xA1FB	0x2198	#SOUTH EAST ARROW
                     test_uchar(0xA1),
                     test_uchar(0xFB),
                     //
                     // 0xA1FC	0x2225	#PARALLEL TO
                     test_uchar(0xA1),
                     test_uchar(0xFC),
                     //
                     // 0xA1FD	0x2223	#DIVIDES
                     test_uchar(0xA1),
                     test_uchar(0xFD),
                     //
                     // 0xA1FE	0xFF0F	#FULLWIDTH SOLIDUS
                     test_uchar(0xA1),
                     test_uchar(0xFE),
                     //
                     // 0xA240	0xFF3C	#FULLWIDTH REVERSE SOLIDUS
                     test_uchar(0xA2),
                     test_uchar(0x40),
                     //
                     // 0xA241	0x2215	#DIVISION SLASH
                     test_uchar(0xA2),
                     test_uchar(0x41),
                     //
                     // 0xA242	0xFE68	#SMALL REVERSE SOLIDUS
                     test_uchar(0xA2),
                     test_uchar(0x42),
                 },
                 L"\u300E"
                 L"\u300F"
                 L"\uFE43"
                 L"\uFE44"
                 L"\uFE59"
                 L"\uFE5A"
                 L"\uFE5B"
                 L"\uFE5C"
                 L"\uFE5D"
                 L"\uFE5E"
                 L"\u2018"
                 L"\u2019"
                 L"\u201C"
                 L"\u201D"
                 L"\u301D"
                 L"\u301E"
                 L"\u2035"
                 L"\u2032"
                 L"\uFF03"
                 L"\uFF06"
                 L"\uFF0A"
                 L"\u203B"
                 L"\u00A7"
                 L"\u3003"
                 L"\u25CB"
                 L"\u25CF"
                 L"\u25B3"
                 L"\u25B2"
                 L"\u25CE"
                 L"\u2606"
                 L"\u2605"
                 L"\u25C7"
                 L"\u25C6"
                 L"\u25A1"
                 L"\u25A0"
                 L"\u25BD"
                 L"\u25BC"
                 L"\u32A3"
                 L"\u2105"
                 L"\u00AF"
                 L"\uFFE3"
                 L"\uFF3F"
                 L"\u02CD"
                 L"\uFE49"
                 L"\uFE4A"
                 L"\uFE4D"
                 L"\uFE4E"
                 L"\uFE4B"
                 L"\uFE4C"
                 L"\uFE5F"
                 L"\uFE60"
                 L"\uFE61"
                 L"\uFF0B"
                 L"\uFF0D"
                 L"\u00D7"
                 L"\u00F7"
                 L"\u00B1"
                 L"\u221A"
                 L"\uFF1C"
                 L"\uFF1E"
                 L"\uFF1D"
                 L"\u2266"
                 L"\u2267"
                 L"\u2260"
                 L"\u221E"
                 L"\u2252"
                 L"\u2261"
                 L"\uFE62"
                 L"\uFE63"
                 L"\uFE64"
                 L"\uFE65"
                 L"\uFE66"
                 L"\uFF5E"
                 L"\u2229"
                 L"\u222A"
                 L"\u22A5"
                 L"\u2220"
                 L"\u221F"
                 L"\u22BF"
                 L"\u33D2"
                 L"\u33D1"
                 L"\u222B"
                 L"\u222E"
                 L"\u2235"
                 L"\u2234"
                 L"\u2640"
                 L"\u2642"
                 L"\u2295"
                 L"\u2299"
                 L"\u2191"
                 L"\u2193"
                 L"\u2190"
                 L"\u2192"
                 L"\u2196"
                 L"\u2197"
                 L"\u2199"
                 L"\u2198"
                 L"\u2225"
                 L"\u2223"
                 L"\uFF0F"
                 L"\uFF3C"
                 L"\u2215"
                 L"\uFE68"sv);

// t7 is t6 with the extra character added

inline auto mb_cp950_t7 =
    mb_test_data(m::multi_byte::code_page{950},
                 {
                     //
                     // 0xA179	0x300E	#LEFT WHITE CORNER BRACKET
                     test_uchar(0xA1),
                     test_uchar(0x79),
                     //
                     // 0xA17A	0x300F	#RIGHT WHITE CORNER BRACKET
                     test_uchar(0xA1),
                     test_uchar(0x7A),
                     //
                     // 0xA17B	0xFE43	#PRESENTATION FORM FOR VERTICAL LEFT WHITE CORNER BRACKET
                     test_uchar(0xA1),
                     test_uchar(0x7B),
                     //
                     // 0xA17C	0xFE44	#PRESENTATION FORM FOR VERTICAL RIGHT WHITE CORNER BRACKET
                     test_uchar(0xA1),
                     test_uchar(0x7C),
                     //
                     // 0xA17D	0xFE59	#SMALL LEFT PARENTHESIS
                     test_uchar(0xA1),
                     test_uchar(0x7D),
                     //
                     // 0xA17E	0xFE5A	#SMALL RIGHT PARENTHESIS
                     test_uchar(0xA1),
                     test_uchar(0x7E),
                     // dash, added at character 7
                     '-',
                     //
                     // 0xA1A1	0xFE5B	#SMALL LEFT CURLY BRACKET
                     test_uchar(0xA1),
                     test_uchar(0xA1),
                     //
                     // 0xA1A2	0xFE5C	#SMALL RIGHT CURLY BRACKET
                     test_uchar(0xA1),
                     test_uchar(0xA2),
                     //
                     // 0xA1A3	0xFE5D	#SMALL LEFT TORTOISE SHELL BRACKET
                     test_uchar(0xA1),
                     test_uchar(0xA3),
                     //
                     // 0xA1A4	0xFE5E	#SMALL RIGHT TORTOISE SHELL BRACKET
                     test_uchar(0xA1),
                     test_uchar(0xA4),
                     //
                     // 0xA1A5	0x2018	#LEFT SINGLE QUOTATION MARK
                     test_uchar(0xA1),
                     test_uchar(0xA5),
                     //
                     // 0xA1A6	0x2019	#RIGHT SINGLE QUOTATION MARK
                     test_uchar(0xA1),
                     test_uchar(0xA6),
                     //
                     // 0xA1A7	0x201C	#LEFT DOUBLE QUOTATION MARK
                     test_uchar(0xA1),
                     test_uchar(0xA7),
                     //
                     // 0xA1A8	0x201D	#RIGHT DOUBLE QUOTATION MARK
                     test_uchar(0xA1),
                     test_uchar(0xA8),
                     //
                     // 0xA1A9	0x301D	#REVERSED DOUBLE PRIME QUOTATION MARK
                     test_uchar(0xA1),
                     test_uchar(0xA9),
                     //
                     // 0xA1AA	0x301E	#DOUBLE PRIME QUOTATION MARK
                     test_uchar(0xA1),
                     test_uchar(0xAA),
                     //
                     // 0xA1AB	0x2035	#REVERSED PRIME
                     test_uchar(0xA1),
                     test_uchar(0xAB),
                     //
                     // 0xA1AC	0x2032	#PRIME
                     test_uchar(0xA1),
                     test_uchar(0xAC),
                     //
                     // 0xA1AD	0xFF03	#FULLWIDTH NUMBER SIGN
                     test_uchar(0xA1),
                     test_uchar(0xAD),
                     //
                     // 0xA1AE	0xFF06	#FULLWIDTH AMPERSAND
                     test_uchar(0xA1),
                     test_uchar(0xAE),
                     //
                     // 0xA1AF	0xFF0A	#FULLWIDTH ASTERISK
                     test_uchar(0xA1),
                     test_uchar(0xAF),
                     //
                     // 0xA1B0	0x203B	#REFERENCE MARK
                     test_uchar(0xA1),
                     test_uchar(0xB0),
                     //
                     // 0xA1B1	0x00A7	#SECTION SIGN
                     test_uchar(0xA1),
                     test_uchar(0xB1),
                     //
                     // 0xA1B2	0x3003	#DITTO MARK
                     test_uchar(0xA1),
                     test_uchar(0xB2),
                     //
                     // 0xA1B3	0x25CB	#WHITE CIRCLE
                     test_uchar(0xA1),
                     test_uchar(0xB3),
                     //
                     // 0xA1B4	0x25CF	#BLACK CIRCLE
                     test_uchar(0xA1),
                     test_uchar(0xB4),
                     //
                     // 0xA1B5	0x25B3	#WHITE UP-POINTING TRIANGLE
                     test_uchar(0xA1),
                     test_uchar(0xB5),
                     //
                     // 0xA1B6	0x25B2	#BLACK UP-POINTING TRIANGLE
                     test_uchar(0xA1),
                     test_uchar(0xB6),
                     //
                     // 0xA1B7	0x25CE	#BULLSEYE
                     test_uchar(0xA1),
                     test_uchar(0xB7),
                     //
                     // 0xA1B8	0x2606	#WHITE STAR
                     test_uchar(0xA1),
                     test_uchar(0xB8),
                     //
                     // 0xA1B9	0x2605	#BLACK STAR
                     test_uchar(0xA1),
                     test_uchar(0xB9),
                     //
                     // 0xA1BA	0x25C7	#WHITE DIAMOND
                     test_uchar(0xA1),
                     test_uchar(0xBA),
                     //
                     // 0xA1BB	0x25C6	#BLACK DIAMOND
                     test_uchar(0xA1),
                     test_uchar(0xBB),
                     //
                     // 0xA1BC	0x25A1	#WHITE SQUARE
                     test_uchar(0xA1),
                     test_uchar(0xBC),
                     //
                     // 0xA1BD	0x25A0	#BLACK SQUARE
                     test_uchar(0xA1),
                     test_uchar(0xBD),
                     //
                     // 0xA1BE	0x25BD	#WHITE DOWN-POINTING TRIANGLE
                     test_uchar(0xA1),
                     test_uchar(0xBE),
                     //
                     // 0xA1BF	0x25BC	#BLACK DOWN-POINTING TRIANGLE
                     test_uchar(0xA1),
                     test_uchar(0xBF),
                     //
                     // 0xA1C0	0x32A3	#CIRCLED IDEOGRAPH CORRECT
                     test_uchar(0xA1),
                     test_uchar(0xC0),
                     //
                     // 0xA1C1	0x2105	#CARE OF
                     test_uchar(0xA1),
                     test_uchar(0xC1),
                     //
                     // 0xA1C2	0x00AF	#MACRON
                     test_uchar(0xA1),
                     test_uchar(0xC2),
                     //
                     // 0xA1C3	0xFFE3	#FULLWIDTH MACRON
                     test_uchar(0xA1),
                     test_uchar(0xC3),
                     //
                     // 0xA1C4	0xFF3F	#FULLWIDTH LOW LINE
                     test_uchar(0xA1),
                     test_uchar(0xC4),
                     //
                     // 0xA1C5	0x02CD	#MODIFIER LETTER LOW MACRON
                     test_uchar(0xA1),
                     test_uchar(0xC5),
                     //
                     // 0xA1C6	0xFE49	#DASHED OVERLINE
                     test_uchar(0xA1),
                     test_uchar(0xC6),
                     //
                     // 0xA1C7	0xFE4A	#CENTRELINE OVERLINE
                     test_uchar(0xA1),
                     test_uchar(0xC7),
                     //
                     // 0xA1C8	0xFE4D	#DASHED LOW LINE
                     test_uchar(0xA1),
                     test_uchar(0xC8),
                     //
                     // 0xA1C9	0xFE4E	#CENTRELINE LOW LINE
                     test_uchar(0xA1),
                     test_uchar(0xC9),
                     //
                     // 0xA1CA	0xFE4B	#WAVY OVERLINE
                     test_uchar(0xA1),
                     test_uchar(0xCA),
                     //
                     // 0xA1CB	0xFE4C	#DOUBLE WAVY OVERLINE
                     test_uchar(0xA1),
                     test_uchar(0xCB),
                     //
                     // 0xA1CC	0xFE5F	#SMALL NUMBER SIGN
                     test_uchar(0xA1),
                     test_uchar(0xCC),
                     //
                     // 0xA1CD	0xFE60	#SMALL AMPERSAND
                     test_uchar(0xA1),
                     test_uchar(0xCD),
                     //
                     // 0xA1CE	0xFE61	#SMALL ASTERISK
                     test_uchar(0xA1),
                     test_uchar(0xCE),
                     //
                     // 0xA1CF	0xFF0B	#FULLWIDTH PLUS SIGN
                     test_uchar(0xA1),
                     test_uchar(0xCF),
                     //
                     // 0xA1D0	0xFF0D	#FULLWIDTH HYPHEN-MINUS
                     test_uchar(0xA1),
                     test_uchar(0xD0),
                     //
                     // 0xA1D1	0x00D7	#MULTIPLICATION SIGN
                     test_uchar(0xA1),
                     test_uchar(0xD1),
                     //
                     // 0xA1D2	0x00F7	#DIVISION SIGN
                     test_uchar(0xA1),
                     test_uchar(0xD2),
                     //
                     // 0xA1D3	0x00B1	#PLUS-MINUS SIGN
                     test_uchar(0xA1),
                     test_uchar(0xD3),
                     //
                     // 0xA1D4	0x221A	#SQUARE ROOT
                     test_uchar(0xA1),
                     test_uchar(0xD4),
                     //
                     // 0xA1D5	0xFF1C	#FULLWIDTH LESS-THAN SIGN
                     test_uchar(0xA1),
                     test_uchar(0xD5),
                     //
                     // 0xA1D6	0xFF1E	#FULLWIDTH GREATER-THAN SIGN
                     test_uchar(0xA1),
                     test_uchar(0xD6),
                     //
                     // 0xA1D7	0xFF1D	#FULLWIDTH EQUALS SIGN
                     test_uchar(0xA1),
                     test_uchar(0xD7),
                     //
                     // 0xA1D8	0x2266	#LESS-THAN OVER EQUAL TO
                     test_uchar(0xA1),
                     test_uchar(0xD8),
                     //
                     // 0xA1D9	0x2267	#GREATER-THAN OVER EQUAL TO
                     test_uchar(0xA1),
                     test_uchar(0xD9),
                     //
                     // 0xA1DA	0x2260	#NOT EQUAL TO
                     test_uchar(0xA1),
                     test_uchar(0xDA),
                     //
                     // 0xA1DB	0x221E	#INFINITY
                     test_uchar(0xA1),
                     test_uchar(0xDB),
                     //
                     // 0xA1DC	0x2252	#APPROXIMATELY EQUAL TO OR THE IMAGE OF
                     test_uchar(0xA1),
                     test_uchar(0xDC),
                     //
                     // 0xA1DD	0x2261	#IDENTICAL TO
                     test_uchar(0xA1),
                     test_uchar(0xDD),
                     //
                     // 0xA1DE	0xFE62	#SMALL PLUS SIGN
                     test_uchar(0xA1),
                     test_uchar(0xDE),
                     //
                     // 0xA1DF	0xFE63	#SMALL HYPHEN-MINUS
                     test_uchar(0xA1),
                     test_uchar(0xDF),
                     //
                     // 0xA1E0	0xFE64	#SMALL LESS-THAN SIGN
                     test_uchar(0xA1),
                     test_uchar(0xE0),
                     //
                     // 0xA1E1	0xFE65	#SMALL GREATER-THAN SIGN
                     test_uchar(0xA1),
                     test_uchar(0xE1),
                     //
                     // 0xA1E2	0xFE66	#SMALL EQUALS SIGN
                     test_uchar(0xA1),
                     test_uchar(0xE2),
                     //
                     // 0xA1E3	0xFF5E	#FULLWIDTH TILDE
                     test_uchar(0xA1),
                     test_uchar(0xE3),
                     //
                     // 0xA1E4	0x2229	#INTERSECTION
                     test_uchar(0xA1),
                     test_uchar(0xE4),
                     //
                     // 0xA1E5	0x222A	#UNION
                     test_uchar(0xA1),
                     test_uchar(0xE5),
                     //
                     // 0xA1E6	0x22A5	#UP TACK
                     test_uchar(0xA1),
                     test_uchar(0xE6),
                     //
                     // 0xA1E7	0x2220	#ANGLE
                     test_uchar(0xA1),
                     test_uchar(0xE7),
                     //
                     // 0xA1E8	0x221F	#RIGHT ANGLE
                     test_uchar(0xA1),
                     test_uchar(0xE8),
                     //
                     // 0xA1E9	0x22BF	#RIGHT TRIANGLE
                     test_uchar(0xA1),
                     test_uchar(0xE9),
                     //
                     // 0xA1EA	0x33D2	#SQUARE LOG
                     test_uchar(0xA1),
                     test_uchar(0xEA),
                     //
                     // 0xA1EB	0x33D1	#SQUARE LN
                     test_uchar(0xA1),
                     test_uchar(0xEB),
                     //
                     // 0xA1EC	0x222B	#INTEGRAL
                     test_uchar(0xA1),
                     test_uchar(0xEC),
                     //
                     // 0xA1ED	0x222E	#CONTOUR INTEGRAL
                     test_uchar(0xA1),
                     test_uchar(0xED),
                     //
                     // 0xA1EE	0x2235	#BECAUSE
                     test_uchar(0xA1),
                     test_uchar(0xEE),
                     //
                     // 0xA1EF	0x2234	#THEREFORE
                     test_uchar(0xA1),
                     test_uchar(0xEF),
                     //
                     // 0xA1F0	0x2640	#FEMALE SIGN
                     test_uchar(0xA1),
                     test_uchar(0xF0),
                     //
                     // 0xA1F1	0x2642	#MALE SIGN
                     test_uchar(0xA1),
                     test_uchar(0xF1),
                     //
                     // 0xA1F2	0x2295	#CIRCLED PLUS
                     test_uchar(0xA1),
                     test_uchar(0xF2),
                     //
                     // 0xA1F3	0x2299	#CIRCLED DOT OPERATOR
                     test_uchar(0xA1),
                     test_uchar(0xF3),
                     //
                     // 0xA1F4	0x2191	#UPWARDS ARROW
                     test_uchar(0xA1),
                     test_uchar(0xF4),
                     //
                     // 0xA1F5	0x2193	#DOWNWARDS ARROW
                     test_uchar(0xA1),
                     test_uchar(0xF5),
                     //
                     // 0xA1F6	0x2190	#LEFTWARDS ARROW
                     test_uchar(0xA1),
                     test_uchar(0xF6),
                     //
                     // 0xA1F7	0x2192	#RIGHTWARDS ARROW
                     test_uchar(0xA1),
                     test_uchar(0xF7),
                     //
                     // 0xA1F8	0x2196	#NORTH WEST ARROW
                     test_uchar(0xA1),
                     test_uchar(0xF8),
                     //
                     // 0xA1F9	0x2197	#NORTH EAST ARROW
                     test_uchar(0xA1),
                     test_uchar(0xF9),
                     //
                     // 0xA1FA	0x2199	#SOUTH WEST ARROW
                     test_uchar(0xA1),
                     test_uchar(0xFA),
                     //
                     // 0xA1FB	0x2198	#SOUTH EAST ARROW
                     test_uchar(0xA1),
                     test_uchar(0xFB),
                     //
                     // 0xA1FC	0x2225	#PARALLEL TO
                     test_uchar(0xA1),
                     test_uchar(0xFC),
                     //
                     // 0xA1FD	0x2223	#DIVIDES
                     test_uchar(0xA1),
                     test_uchar(0xFD),
                     //
                     // 0xA1FE	0xFF0F	#FULLWIDTH SOLIDUS
                     test_uchar(0xA1),
                     test_uchar(0xFE),
                     //
                     // 0xA240	0xFF3C	#FULLWIDTH REVERSE SOLIDUS
                     test_uchar(0xA2),
                     test_uchar(0x40),
                     //
                     // 0xA241	0x2215	#DIVISION SLASH
                     test_uchar(0xA2),
                     test_uchar(0x41),
                     //
                     // 0xA242	0xFE68	#SMALL REVERSE SOLIDUS
                     test_uchar(0xA2),
                     test_uchar(0x42),
                 },
                 L"\u300E"
                 L"\u300F"
                 L"\uFE43"
                 L"\uFE44"
                 L"\uFE59"
                 L"\uFE5A"
                 L"-"
                 L"\uFE5B"
                 L"\uFE5C"
                 L"\uFE5D"
                 L"\uFE5E"
                 L"\u2018"
                 L"\u2019"
                 L"\u201C"
                 L"\u201D"
                 L"\u301D"
                 L"\u301E"
                 L"\u2035"
                 L"\u2032"
                 L"\uFF03"
                 L"\uFF06"
                 L"\uFF0A"
                 L"\u203B"
                 L"\u00A7"
                 L"\u3003"
                 L"\u25CB"
                 L"\u25CF"
                 L"\u25B3"
                 L"\u25B2"
                 L"\u25CE"
                 L"\u2606"
                 L"\u2605"
                 L"\u25C7"
                 L"\u25C6"
                 L"\u25A1"
                 L"\u25A0"
                 L"\u25BD"
                 L"\u25BC"
                 L"\u32A3"
                 L"\u2105"
                 L"\u00AF"
                 L"\uFFE3"
                 L"\uFF3F"
                 L"\u02CD"
                 L"\uFE49"
                 L"\uFE4A"
                 L"\uFE4D"
                 L"\uFE4E"
                 L"\uFE4B"
                 L"\uFE4C"
                 L"\uFE5F"
                 L"\uFE60"
                 L"\uFE61"
                 L"\uFF0B"
                 L"\uFF0D"
                 L"\u00D7"
                 L"\u00F7"
                 L"\u00B1"
                 L"\u221A"
                 L"\uFF1C"
                 L"\uFF1E"
                 L"\uFF1D"
                 L"\u2266"
                 L"\u2267"
                 L"\u2260"
                 L"\u221E"
                 L"\u2252"
                 L"\u2261"
                 L"\uFE62"
                 L"\uFE63"
                 L"\uFE64"
                 L"\uFE65"
                 L"\uFE66"
                 L"\uFF5E"
                 L"\u2229"
                 L"\u222A"
                 L"\u22A5"
                 L"\u2220"
                 L"\u221F"
                 L"\u22BF"
                 L"\u33D2"
                 L"\u33D1"
                 L"\u222B"
                 L"\u222E"
                 L"\u2235"
                 L"\u2234"
                 L"\u2640"
                 L"\u2642"
                 L"\u2295"
                 L"\u2299"
                 L"\u2191"
                 L"\u2193"
                 L"\u2190"
                 L"\u2192"
                 L"\u2196"
                 L"\u2197"
                 L"\u2199"
                 L"\u2198"
                 L"\u2225"
                 L"\u2223"
                 L"\uFF0F"
                 L"\uFF3C"
                 L"\u2215"
                 L"\uFE68"sv);
