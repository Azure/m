// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//
// Extended tests for multi_byte conversion bugs:
//
//  Bug 1 — multi_byte_to_utf16 span overload with ec: on failure the output span was not
//           zeroed, leaving callers (resize_and_overwrite based) with garbage string content.
//
//  Bug 2 — utf16_to_multi_byte span overload with ec: same problem on the reverse path.
//
//  Bug 3 — view_to_span(cp, wstring_view, span<char>&, ec) was never defined despite being
//           declared in the header.  Every call to view_to_span with a u16string_view + ec
//           cascaded through it, causing an unresolved external symbol.
//

#include <gtest/gtest.h>

#include <array>
#include <string_view>
#include <system_error>

#include <m/multi_byte/convert.h>
#include <m/utility/make_span.h>

#include <Windows.h>

#include "multi_byte_test_data.h"

using namespace std::string_view_literals;

// CP-950 (Big5) is the code page exercised throughout this test suite.
constexpr m::multi_byte::code_page dbcs_cp = m::multi_byte::code_page{950};

// An invalid CP-950 byte sequence: the high byte 0x81 requires a valid DBCS
// trail byte, but 0x20 (space) is not a legal trail byte.
std::array<char const, 3> const bad_mbcs_bytes{'x', '\x81', '\x20'};
auto const bad_mbcs_sv = std::string_view(bad_mbcs_bytes.data(), bad_mbcs_bytes.size());

// Helper: return true if the code page is installed so tests can be skipped gracefully.
static bool cp_is_available(m::multi_byte::code_page cp)
{
    return ::IsValidCodePage(m::to_underlying(cp)) != 0;
}

// ──────────────────────────────────────────────────────────────────────────────
// Bug 1 regression: multi_byte_to_utf16 span+ec path
//
// Before the fix, a MultiByteToWideChar failure left the output span at its
// original (pre-allocated) size instead of 0.  The caller's resize_and_overwrite
// lambda then returned that non-zero size, populating the string with garbage.
// After the fix the span is zeroed so the lambda returns 0 and the string is empty.
// ──────────────────────────────────────────────────────────────────────────────
TEST(MultiByteExtended, MultiByteToUtf16SpanEcFailureLeavesSpanEmpty)
{
    if (!cp_is_available(dbcs_cp))
        GTEST_SKIP() << "Code page 950 not available on this machine";

    // Allocate a span large enough that garbage would be visible if not zeroed.
    std::array<wchar_t, 32> buf{};
    auto span = std::span<wchar_t>(buf.data(), buf.size());

    std::error_code ec;
    m::multi_byte_to_utf16(dbcs_cp, bad_mbcs_sv, span, ec);

    EXPECT_TRUE(ec) << "Expected an error for invalid DBCS sequence";
    EXPECT_EQ(span.size(), 0u) << "Output span must be empty on failure (Bug 1)";
}

// ──────────────────────────────────────────────────────────────────────────────
// Bug 2 regression: utf16_to_multi_byte span+ec path
//
// A too-small output buffer causes WideCharToMultiByte to fail with
// ERROR_INSUFFICIENT_BUFFER.  Before the fix the span retained its original size;
// after the fix it is zeroed.
//
// mb_cp950_t2 encodes 4 wide characters each requiring 2 CP-950 bytes → 8 bytes
// total.  Providing only a 4-byte buffer is guaranteed to be insufficient.
// ──────────────────────────────────────────────────────────────────────────────
TEST(MultiByteExtended, Utf16ToMultiByteSpanEcFailureLeavesSpanEmpty)
{
    if (!cp_is_available(dbcs_cp))
        GTEST_SKIP() << "Code page 950 not available on this machine";

    // mb_cp950_t2.m_wview is L"\ufe6b\u33d5\u5159\u2588" — 4 wchars, 8 CP-950 bytes.
    // A 4-char buffer is smaller than the 8 bytes required, so the conversion must fail.
    std::array<char, 4> buf{};
    auto span = std::span<char>(buf.data(), buf.size());

    std::error_code ec;
    m::utf16_to_multi_byte(dbcs_cp, mb_cp950_t2.m_wview, span, ec);

    EXPECT_TRUE(ec) << "Expected an error for too-small output buffer";
    EXPECT_EQ(span.size(), 0u) << "Output span must be empty on failure (Bug 2)";
}

// ──────────────────────────────────────────────────────────────────────────────
// Bug 3 regression: view_to_span(cp, wstring_view, span<char>&, ec) must exist.
//
// Happy-path test confirms the specialization is reachable and produces correct
// output.  mb_cp950_t1 is L"\ufe6b" (1 wide char) → {0xa2, 0x4e} in CP-950.
// ──────────────────────────────────────────────────────────────────────────────
TEST(MultiByteExtended, ViewToSpanWstringViewEcHappyPath)
{
    if (!cp_is_available(dbcs_cp))
        GTEST_SKIP() << "Code page 950 not available on this machine";

    std::array<char, 8> buf{};
    auto span = std::span<char>(buf.data(), buf.size());

    std::error_code ec;
    m::view_to_span(dbcs_cp, mb_cp950_t1.m_wview, span, ec);

    EXPECT_FALSE(ec) << "Unexpected error: " << ec.message();
    EXPECT_EQ(span.size(), mb_cp950_t1.m_view.size());
    EXPECT_EQ(std::string_view(span.data(), span.size()), mb_cp950_t1.m_view);
}

// ──────────────────────────────────────────────────────────────────────────────
// view_to_span with u16string_view + ec: exercises the u16string_view specialization
// which delegates to the (previously missing) wstring_view+ec specialization.
// ──────────────────────────────────────────────────────────────────────────────
TEST(MultiByteExtended, ViewToSpanU16StringViewEcHappyPath)
{
    if (!cp_is_available(dbcs_cp))
        GTEST_SKIP() << "Code page 950 not available on this machine";

    std::array<char, 8> buf{};
    auto span = std::span<char>(buf.data(), buf.size());

    std::error_code ec;
    m::view_to_span(dbcs_cp, mb_cp950_t1.m_u16view, span, ec);

    EXPECT_FALSE(ec) << "Unexpected error: " << ec.message();
    EXPECT_EQ(span.size(), mb_cp950_t1.m_view.size());
    EXPECT_EQ(std::string_view(span.data(), span.size()), mb_cp950_t1.m_view);
}
