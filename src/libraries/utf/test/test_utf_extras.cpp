// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <m/utf/decode.h>
#include <m/utf/decode_iterator.h>
#include <m/utf/decode_result.h>
#include <m/utf/encode.h>
#include <m/utf/exceptions.h>
#include <m/utf/transcode.h>

#include "test_data.h"

using namespace std::string_view_literals;

// ===========================================================================
// encode_utf8 — typed iterator version (char8_t output)
// ===========================================================================

TEST(EncodeUtf8, Ascii)
{
    std::u8string buf;
    m::utf::encode_utf8<char8_t>(U'A', std::back_inserter(buf));
    EXPECT_EQ(buf, u8"A");
    EXPECT_EQ(buf.size(), 1u);
}

TEST(EncodeUtf8, TwoByteSequence)
{
    // U+00C9 É → UTF-8: C3 89
    std::u8string buf;
    m::utf::encode_utf8<char8_t>(0x00C9, std::back_inserter(buf));
    ASSERT_EQ(buf.size(), 2u);
    EXPECT_EQ(static_cast<uint8_t>(buf[0]), 0xC3u);
    EXPECT_EQ(static_cast<uint8_t>(buf[1]), 0x89u);
}

TEST(EncodeUtf8, ThreeByteSequence)
{
    // U+4E2D 中 → UTF-8: E4 B8 AD
    std::u8string buf;
    m::utf::encode_utf8<char8_t>(0x4E2D, std::back_inserter(buf));
    ASSERT_EQ(buf.size(), 3u);
    EXPECT_EQ(static_cast<uint8_t>(buf[0]), 0xE4u);
    EXPECT_EQ(static_cast<uint8_t>(buf[1]), 0xB8u);
    EXPECT_EQ(static_cast<uint8_t>(buf[2]), 0xADu);
}

TEST(EncodeUtf8, FourByteSequence)
{
    // U+1F600 😀 → UTF-8: F0 9F 98 80
    std::u8string buf;
    m::utf::encode_utf8<char8_t>(0x1F600, std::back_inserter(buf));
    ASSERT_EQ(buf.size(), 4u);
    EXPECT_EQ(static_cast<uint8_t>(buf[0]), 0xF0u);
    EXPECT_EQ(static_cast<uint8_t>(buf[1]), 0x9Fu);
    EXPECT_EQ(static_cast<uint8_t>(buf[2]), 0x98u);
    EXPECT_EQ(static_cast<uint8_t>(buf[3]), 0x80u);
}

TEST(EncodeUtf8, MaxValidCodepoint)
{
    // U+10FFFF — maximum valid Unicode codepoint → F4 8F BF BF
    std::u8string buf;
    m::utf::encode_utf8<char8_t>(0x10FFFF, std::back_inserter(buf));
    ASSERT_EQ(buf.size(), 4u);
    EXPECT_EQ(static_cast<uint8_t>(buf[0]), 0xF4u);
    EXPECT_EQ(static_cast<uint8_t>(buf[1]), 0x8Fu);
    EXPECT_EQ(static_cast<uint8_t>(buf[2]), 0xBFu);
    EXPECT_EQ(static_cast<uint8_t>(buf[3]), 0xBFu);
}

TEST(EncodeUtf8, RejectsLoneLowSurrogate)
{
    std::u8string buf;
    EXPECT_THROW(m::utf::encode_utf8<char8_t>(0xDC00, std::back_inserter(buf)),
                 std::runtime_error);
}

TEST(EncodeUtf8, RejectsAboveUnicode)
{
    std::u8string buf;
    EXPECT_THROW(m::utf::encode_utf8<char8_t>(0x110000, std::back_inserter(buf)),
                 std::runtime_error);
}

// ===========================================================================
// encode_utf8 — error_code variant
// ===========================================================================

TEST(EncodeUtf8_ErrorCode, ValidAscii)
{
    std::u8string   buf;
    std::error_code ec;
    m::utf::encode_utf8<char8_t>(U'Z', std::back_inserter(buf), ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(buf, u8"Z");
}

TEST(EncodeUtf8_ErrorCode, LoneSurrogate)
{
    std::u8string   buf;
    std::error_code ec;
    m::utf::encode_utf8<char8_t>(0xDFFF, std::back_inserter(buf), ec);
    EXPECT_TRUE(ec);
    EXPECT_TRUE(buf.empty()); // no bytes written on error
}

// ===========================================================================
// encode_utf16 — typed output (char16_t)
// ===========================================================================

TEST(EncodeUtf16, BmpCharacter)
{
    std::u16string buf;
    m::utf::encode_utf16<char16_t>(U'A', std::back_inserter(buf));
    ASSERT_EQ(buf.size(), 1u);
    EXPECT_EQ(buf[0], u'A');
}

TEST(EncodeUtf16, SurrogatePair)
{
    // U+1F600 😀 → D83D DE00
    std::u16string buf;
    m::utf::encode_utf16<char16_t>(0x1F600, std::back_inserter(buf));
    ASSERT_EQ(buf.size(), 2u);
    EXPECT_EQ(buf[0], char16_t{0xD83D});
    EXPECT_EQ(buf[1], char16_t{0xDE00});
}

TEST(EncodeUtf16, RejectsLoneSurrogate)
{
    std::u16string buf;
    EXPECT_THROW(m::utf::encode_utf16<char16_t>(0xDC00, std::back_inserter(buf)),
                 std::runtime_error);
}

TEST(EncodeUtf16, RejectsAboveUnicode)
{
    std::u16string buf;
    EXPECT_THROW(m::utf::encode_utf16<char16_t>(0x110000, std::back_inserter(buf)),
                 std::runtime_error);
}

// ===========================================================================
// compute sizing functions
// ===========================================================================

TEST(ComputeEncodedSize, Utf8)
{
    EXPECT_EQ(m::utf::compute_encoded_utf8_size(U'A'),     1u); // ASCII
    EXPECT_EQ(m::utf::compute_encoded_utf8_size(0x00C9u),  2u); // U+00C9 É
    EXPECT_EQ(m::utf::compute_encoded_utf8_size(0x4E2Du),  3u); // U+4E2D 中
    EXPECT_EQ(m::utf::compute_encoded_utf8_size(0x1F600u), 4u); // U+1F600 😀
}

TEST(ComputeEncodedSize, Utf8_MaxValid)
{
    // U+10FFFF is the maximum valid codepoint, must encode to 4 bytes
    EXPECT_EQ(m::utf::compute_encoded_utf8_size(0x10FFFFu), 4u);
}

TEST(ComputeEncodedSize, Utf16_BmpVsSurrogatePair)
{
    EXPECT_EQ(m::utf::compute_encoded_utf16_count(U'A'),     1u);
    EXPECT_EQ(m::utf::compute_encoded_utf16_count(0x1F600u), 2u);
    EXPECT_EQ(m::utf::compute_encoded_utf16_bytes(U'A'),     2u);
    EXPECT_EQ(m::utf::compute_encoded_utf16_bytes(0x1F600u), 4u);
}

TEST(ComputeEncodedSize, Utf32_AlwaysOneCountFourBytes)
{
    EXPECT_EQ(m::utf::compute_encoded_utf32_count(U'A'),     1u);
    EXPECT_EQ(m::utf::compute_encoded_utf32_count(0x10FFFFu), 1u);
    EXPECT_EQ(m::utf::compute_encoded_utf32_bytes(U'A'),     4u);
    EXPECT_EQ(m::utf::compute_encoded_utf32_bytes(0x10FFFFu), 4u);
}

// ===========================================================================
// decode_utf8 — error_code variant
// ===========================================================================

TEST(DecodeUtf8_ErrorCode, ValidAscii)
{
    std::array<char8_t, 1> data = {char8_t{'A'}};
    std::error_code         ec;
    auto res = m::utf::decode_utf8(data.begin(), data.end(), ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(res.ch, U'A');
}

TEST(DecodeUtf8_ErrorCode, ValidTwoByte)
{
    // U+00C9 É → C3 89
    std::array<char8_t, 2> data = {char8_t{0xC3}, char8_t{0x89}};
    std::error_code         ec;
    auto res = m::utf::decode_utf8(data.begin(), data.end(), ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(res.ch, char32_t{0x00C9});
}

TEST(DecodeUtf8_ErrorCode, LoneContinuationByte)
{
    std::array<char8_t, 1> data = {char8_t{0x80}};
    std::error_code         ec;
    std::ignore = m::utf::decode_utf8(data.begin(), data.end(), ec);
    EXPECT_TRUE(ec);
    EXPECT_EQ(ec, std::make_error_code(std::errc::illegal_byte_sequence));
}

TEST(DecodeUtf8_ErrorCode, TruncatedTwoByte)
{
    // Lead byte only; 0xC3 expects one continuation byte
    std::array<char8_t, 1> data = {char8_t{0xC3}};
    std::error_code         ec;
    std::ignore = m::utf::decode_utf8(data.begin(), data.end(), ec);
    EXPECT_TRUE(ec);
}

TEST(DecodeUtf8_ErrorCode, NonShortestTwoByte)
{
    // 0xC0 0x80 encodes code point 0x00 in a 2-byte sequence — non-shortest
    std::array<char8_t, 2> data = {char8_t{0xC0}, char8_t{0x80}};
    std::error_code         ec;
    std::ignore = m::utf::decode_utf8(data.begin(), data.end(), ec);
    EXPECT_TRUE(ec);
}

// ===========================================================================
// decode_utf16 — throwing version error cases
// ===========================================================================

TEST(DecodeUtf16_Throwing, LoneLowSurrogate)
{
    std::array<char16_t, 1> data = {char16_t{0xDC00}};
    EXPECT_THROW(std::ignore = m::utf::decode_utf16(data.begin(), data.end()),
                 std::runtime_error);
}

TEST(DecodeUtf16_Throwing, TruncatedSurrogatePair)
{
    // High surrogate with no following low surrogate
    std::array<char16_t, 1> data = {char16_t{0xD800}};
    EXPECT_THROW(std::ignore = m::utf::decode_utf16(data.begin(), data.end()),
                 std::runtime_error);
}

TEST(DecodeUtf16_Throwing, HighSurrogateFollowedByNonSurrogate)
{
    std::array<char16_t, 2> data = {char16_t{0xD800}, char16_t{'A'}};
    EXPECT_THROW(std::ignore = m::utf::decode_utf16(data.begin(), data.end()),
                 std::runtime_error);
}

// ===========================================================================
// decode_utf16 — error_code variant
// ===========================================================================

TEST(DecodeUtf16_ErrorCode, ValidBmp)
{
    std::array<char16_t, 1> data = {char16_t{0x4E2D}}; // U+4E2D 中
    std::error_code          ec;
    auto res = m::utf::decode_utf16(data.begin(), data.end(), ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(res.ch, char32_t{0x4E2D});
}

TEST(DecodeUtf16_ErrorCode, ValidSurrogatePair)
{
    std::array<char16_t, 2> data = {char16_t{0xD83D}, char16_t{0xDE00}}; // U+1F600
    std::error_code          ec;
    auto res = m::utf::decode_utf16(data.begin(), data.end(), ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(res.ch, char32_t{0x1F600});
}

TEST(DecodeUtf16_ErrorCode, LoneLowSurrogate)
{
    std::array<char16_t, 1> data = {char16_t{0xDC00}};
    std::error_code          ec;
    std::ignore = m::utf::decode_utf16(data.begin(), data.end(), ec);
    EXPECT_TRUE(ec);
}

TEST(DecodeUtf16_ErrorCode, TruncatedSurrogatePair)
{
    std::array<char16_t, 1> data = {char16_t{0xD800}};
    std::error_code          ec;
    std::ignore = m::utf::decode_utf16(data.begin(), data.end(), ec);
    EXPECT_TRUE(ec);
}

// ===========================================================================
// decode_utf32 — error cases (typed iterator)
// ===========================================================================

TEST(DecodeUtf32_Throwing, AboveUnicode)
{
    std::array<char32_t, 1> data = {char32_t{0x110000}};
    EXPECT_THROW(std::ignore = m::utf::decode_utf32(data.begin(), data.end()),
                 std::runtime_error);
}

TEST(DecodeUtf32_ErrorCode, AboveUnicode)
{
    std::array<char32_t, 1> data = {char32_t{0x110000}};
    std::error_code          ec;
    std::ignore = m::utf::decode_utf32(data.begin(), data.end(), ec);
    EXPECT_TRUE(ec);
    EXPECT_EQ(ec, std::make_error_code(std::errc::illegal_byte_sequence));
}

// ===========================================================================
// decode_utf16be byte decoder — Bug 3 regression
// Verifies that a truncated surrogate pair (high surrogate with no low
// surrogate in the byte stream) returns k_partial_encoding, not
// k_invalid_character.  Before the fix, decode_utf16be was inconsistent
// with decode_utf16le on this point.
// ===========================================================================

TEST(ByteDecodeUtf16be_Regression, TruncatedHighSurrogate_IsPartial)
{
    // U+D800 in UTF-16 BE = bytes D8 00, with nothing following
    std::array<uint8_t, 2> raw  = {0xD8u, 0x00u};
    auto                   span = std::as_bytes(std::span{raw});
    auto                   res  = m::utf::decode_utf16be(span);
    EXPECT_EQ(res.m_char, m::utf::k_partial_encoding);
}

TEST(ByteDecodeUtf16be_Regression, LoneLowSurrogate_IsInvalid)
{
    // U+DC00 in UTF-16 BE = bytes DC 00 — low surrogate without preceding high
    std::array<uint8_t, 2> raw  = {0xDCu, 0x00u};
    auto                   span = std::as_bytes(std::span{raw});
    auto                   res  = m::utf::decode_utf16be(span);
    EXPECT_EQ(res.m_char, m::utf::k_invalid_character);
}

// ===========================================================================
// transcode — exercises the two-pass resize_and_overwrite path (Bug 2 regression)
// These tests call the string_view overload directly.
// ===========================================================================

TEST(Transcode_Regression, Utf8ViewToUtf16String)
{
    // Explicitly uses transcode<char8_t, char16_t>(string_view<char8_t>, string<char16_t>&)
    std::u8string_view sv = u8"hello";
    std::u16string     out;
    m::utf::transcode<char8_t, char16_t>(sv, out);
    EXPECT_EQ(out, u"hello");
}

TEST(Transcode_Regression, Utf8ViewToUtf32String)
{
    std::u8string_view sv = u8"hello";
    std::u32string     out;
    m::utf::transcode<char8_t, char32_t>(sv, out);
    EXPECT_EQ(out, U"hello");
}

TEST(Transcode_Regression, EmptyUtf8View)
{
    std::u8string_view sv = u8"";
    std::u16string     out;
    m::utf::transcode<char8_t, char16_t>(sv, out);
    EXPECT_TRUE(out.empty());
}

TEST(Transcode_Regression, MultiByte_Rfc3629Ex1)
{
    // A + U+2262 + U+0391 + '.' via the two-pass string_view overload
    std::u8string_view sv = rfc3629_ex_1.m_u8_sv;
    std::u32string     out;
    m::utf::transcode<char8_t, char32_t>(sv, out);
    std::u32string expected(rfc3629_ex_1.m_u32_chardata.begin(),
                             rfc3629_ex_1.m_u32_chardata.end());
    EXPECT_EQ(out, expected);
}

TEST(Transcode_Regression, ErrorCode_Utf8ViewToUtf16)
{
    std::u8string_view sv = u8"world";
    std::u16string     out;
    std::error_code    ec;
    m::utf::transcode<char8_t, char16_t>(sv, out, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(out, u"world");
}

TEST(Transcode, IteratorBased_Utf8ToUtf16)
{
    // Uses the single-pass string_inserter path (different from two-pass above)
    auto out = m::utf::transcode<char16_t>(std::u8string_view{u8"world"});
    EXPECT_EQ(out, u"world");
}

// ===========================================================================
// ucs_decoder_iterator — exercises pre-increment and post-increment (Bug 1 regression)
// ===========================================================================

TEST(UcsDecoderIterator, PreIncrement_Utf8)
{
    std::u8string_view sv = u8"hello";
    auto               it = m::utf::decode_begin(sv);
    auto               en = m::utf::decode_end(sv);

    std::vector<char32_t> chars;
    while (it != en)
    {
        chars.push_back(*it);
        ++it;
    }

    ASSERT_EQ(chars.size(), 5u);
    EXPECT_EQ(chars[0], U'h');
    EXPECT_EQ(chars[4], U'o');
}

TEST(UcsDecoderIterator, PostIncrement_Regression)
{
    // operator++(int) previously returned ucs_decoder_iterator& to a local (dangling ref).
    // After fix it returns by value.  Calling *it++ must dereference the old position.
    std::u8string_view sv = u8"hi";
    auto               it = m::utf::decode_begin(sv);
    auto               en = m::utf::decode_end(sv);

    std::vector<char32_t> chars;
    while (it != en)
        chars.push_back(*it++); // post-increment

    ASSERT_EQ(chars.size(), 2u);
    EXPECT_EQ(chars[0], U'h');
    EXPECT_EQ(chars[1], U'i');
}

TEST(UcsDecoderIterator, PreIncrement_Utf16)
{
    std::u16string_view sv = u"hello";
    auto                it = m::utf::decode_begin(sv);
    auto                en = m::utf::decode_end(sv);

    std::vector<char32_t> chars;
    while (it != en)
    {
        chars.push_back(*it);
        ++it;
    }

    ASSERT_EQ(chars.size(), 5u);
    EXPECT_EQ(chars[0], U'h');
    EXPECT_EQ(chars[4], U'o');
}

TEST(UcsDecoderIterator, EmptyStringView)
{
    std::u8string_view sv = u8"";
    auto               it = m::utf::decode_begin(sv);
    auto               en = m::utf::decode_end(sv);
    EXPECT_EQ(it, en);
}

TEST(UcsDecoderIterator, SurrogatePairInUtf16)
{
    // U+1F600 😀 encoded as char16_t surrogate pair
    std::array<char16_t, 2> emoji_data = {char16_t{0xD83D}, char16_t{0xDE00}};
    std::u16string_view     sv{emoji_data.data(), emoji_data.size()};
    auto                    it = m::utf::decode_begin(sv);
    auto                    en = m::utf::decode_end(sv);

    std::vector<char32_t> chars;
    while (it != en)
    {
        chars.push_back(*it);
        ++it;
    }

    ASSERT_EQ(chars.size(), 1u);
    EXPECT_EQ(chars[0], char32_t{0x1F600});
}

TEST(UcsDecoderIterator, MultiCharRfc3629Ex1)
{
    // Decode rfc3629 example 1 via the ucs_decoder_iterator on utf-8 view
    std::u8string_view    sv = rfc3629_ex_1.m_u8_sv;
    auto                  it = m::utf::decode_begin(sv);
    auto                  en = m::utf::decode_end(sv);
    std::vector<char32_t> chars;
    while (it != en)
    {
        chars.push_back(*it);
        ++it;
    }
    EXPECT_EQ(chars, rfc3629_ex_1.m_u32_chardata);
}
