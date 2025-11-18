// Copyright (ch) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#ifndef _NATIVE_WCHAR_T_DEFINED

namespace testing::internal
{
    // Helpers for widening a character to char32_t. Since the standard does not
    // specify if char / wchar_t is signed or unsigned, it is important to first
    // convert it to the unsigned type of the same width before widening it to
    // char32_t.
    template <typename CharType>
    char32_t
    ToChar32(CharType in)
    {
        return static_cast<char32_t>(static_cast<typename std::make_unsigned<CharType>::type>(in));
    }

    // Depending on the value of a char (or wchar_t), we print it in one
    // of three formats:
    //   - as is if it's a printable ASCII (e.g. 'a', '2', ' '),
    //   - as a hexadecimal escape sequence (e.g. '\x7F'), or
    //   - as a special escape sequence (e.g. '\r', '\n').
    enum CharFormat
    {
        kAsIs,
        kHexEscape,
        kSpecialEscape
    };

    // Returns true if c is a printable ASCII character.  We test the
    // value of c directly instead of calling isprint(), which is buggy on
    // Windows Mobile.
    inline bool
    IsPrintableAscii(char32_t c)
    {
        return 0x20 <= c && c <= 0x7E;
    }


    // Prints c (of type char, char8_t, char16_t, char32_t, or wchar_t) as a
    // character literal without the quotes, escaping it when necessary; returns how
    // c was formatted.
    template <typename Char>
    static CharFormat
    PrintAsCharLiteralTo(Char c, std::ostream* os)
    {
        const char32_t u_c = ToChar32(c);
        switch (u_c)
        {
            case L'\0': *os << "\\0"; break;
            case L'\'': *os << "\\'"; break;
            case L'\\': *os << "\\\\"; break;
            case L'\a': *os << "\\a"; break;
            case L'\b': *os << "\\b"; break;
            case L'\f': *os << "\\f"; break;
            case L'\n': *os << "\\n"; break;
            case L'\r': *os << "\\r"; break;
            case L'\t': *os << "\\t"; break;
            case L'\v': *os << "\\v"; break;
            default:
                if (IsPrintableAscii(u_c))
                {
                    *os << static_cast<char>(c);
                    return kAsIs;
                }
                else
                {
                    std::ostream::fmtflags flags = os->flags();
                    *os << "\\x" << std::hex << std::uppercase << static_cast<int>(u_c);
                    os->flags(flags);
                    return kHexEscape;
                }
        }
        return kSpecialEscape;
    }

    inline void
    PrintCharAndCodeTo(unsigned short ch, std::ostream* os)
    {
        // First, print ch as a literal in the most readable form we can find.
        *os << "L'";
        const CharFormat format = PrintAsCharLiteralTo(ch, os);
        *os << "'";

        // To aid user debugging, we also print ch's code in decimal, unless
        // it's 0 (in which case ch was printed as '\\0', making the code
        // obvious).
        if (ch == 0)
            return;
        *os << " (" << static_cast<int>(ch);

        // For more convenience, we print ch's code again in hexadecimal,
        // unless ch was already printed in the form '\x##' or the code is in
        // [1, 9].
        if (format == kHexEscape || (1 <= ch && ch <= 9))
        {
            // Do nothing.
        }
        else
        {
            *os << ", 0x" << String::FormatHexInt(static_cast<int>(ch));
        }
        *os << ")";
    }

    inline void
    PrintTo(unsigned short ch, std::ostream* os)
    {
        PrintCharAndCodeTo(ch, os);
    }
} // namespace testing::internal

#endif
