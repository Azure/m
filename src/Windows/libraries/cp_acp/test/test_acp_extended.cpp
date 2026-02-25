// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

// Extended tests for cp_acp API — covering paths the main test file omits:
//   * acp_to_string (char → char identity), via value-return and out-param forms
//   * acp_to_basic_string<char>
//   * error_code overloads (happy path for all converters)
//   * empty string inputs
//   * optional overloads from to_string.h

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include <m/cp_acp/convert.h>
#include <m/cp_acp/to_string.h>
#include <m/windows_strings/convert.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

// ---------------------------------------------------------------------------
// acp_to_string (char → char identity)
// ---------------------------------------------------------------------------

TEST(AcpAPIs_Extended, acp_to_string_value_return)
{
    char const* p = "hello";
    EXPECT_EQ(m::acp_to_string("hello"sv), "hello"s);
    EXPECT_EQ(m::acp_to_string("hello"s), "hello"s);
    EXPECT_EQ(m::acp_to_string(p), "hello"s);
}

TEST(AcpAPIs_Extended, acp_to_string_out_param)
{
    std::string out;
    m::acp_to_string("world"sv, out);
    EXPECT_EQ(out, "world"s);

    std::string out2;
    m::acp_to_string("world"s, out2);
    EXPECT_EQ(out2, "world"s);
}

TEST(AcpAPIs_Extended, acp_to_string_ec_value_return)
{
    std::error_code ec;
    auto            r = m::acp_to_string("test"sv, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(r, "test"s);
}

TEST(AcpAPIs_Extended, acp_to_string_ec_out_param)
{
    std::string     out;
    std::error_code ec;
    m::acp_to_string("test"sv, out, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(out, "test"s);
}

// ---------------------------------------------------------------------------
// acp_to_basic_string<char>
// ---------------------------------------------------------------------------

TEST(AcpAPIs_Extended, acp_to_basic_string_char_value_return)
{
    EXPECT_EQ(m::acp_to_basic_string<char>("abc"sv), "abc"s);
    EXPECT_EQ(m::acp_to_basic_string<char>("abc"s), "abc"s);
    EXPECT_EQ(m::acp_to_basic_string<char>("abc"), "abc"s);
}

TEST(AcpAPIs_Extended, acp_to_basic_string_char_out_param)
{
    std::string out;
    m::acp_to_basic_string("xyz"sv, out);
    EXPECT_EQ(out, "xyz"s);
}

TEST(AcpAPIs_Extended, acp_to_basic_string_char_ec_value_return)
{
    std::error_code ec;
    auto            r = m::acp_to_basic_string<char>("test"sv, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(r, "test"s);
}

TEST(AcpAPIs_Extended, acp_to_basic_string_char_ec_out_param)
{
    std::string     out;
    std::error_code ec;
    m::acp_to_basic_string("test"sv, out, ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(out, "test"s);
}

// ---------------------------------------------------------------------------
// Empty string inputs
// ---------------------------------------------------------------------------

TEST(AcpAPIs_Extended, empty_string_acp_to_string)
{
    EXPECT_TRUE(m::acp_to_string(""sv).empty());
    EXPECT_TRUE(m::acp_to_string(""s).empty());

    std::string out{"nonempty"};
    m::acp_to_string(""sv, out);
    EXPECT_TRUE(out.empty());
}

TEST(AcpAPIs_Extended, empty_string_acp_to_wstring)
{
    EXPECT_TRUE(m::acp_to_wstring(""sv).empty());

    std::wstring out{L"nonempty"};
    m::acp_to_wstring(""sv, out);
    EXPECT_TRUE(out.empty());
}

TEST(AcpAPIs_Extended, empty_string_acp_to_u8string)
{
    EXPECT_TRUE(m::acp_to_u8string(""sv).empty());
}

TEST(AcpAPIs_Extended, empty_string_acp_to_u16string)
{
    EXPECT_TRUE(m::acp_to_u16string(""sv).empty());
}

TEST(AcpAPIs_Extended, empty_string_acp_to_u32string)
{
    EXPECT_TRUE(m::acp_to_u32string(""sv).empty());
}

TEST(AcpAPIs_Extended, empty_string_to_acp)
{
    EXPECT_TRUE(m::to_acp_string(""sv).empty());
    EXPECT_TRUE(m::to_acp_string(L""sv).empty());
    EXPECT_TRUE(m::to_acp_string(u""sv).empty());
    EXPECT_TRUE(m::to_acp_string(u8""sv).empty());
}

// ---------------------------------------------------------------------------
// error_code overloads — other converters (happy path)
// ---------------------------------------------------------------------------

TEST(AcpAPIs_Extended, acp_to_wstring_ec)
{
    {
        std::wstring    out;
        std::error_code ec;
        m::acp_to_wstring("hello"sv, out, ec);
        EXPECT_FALSE(ec);
        EXPECT_EQ(out, L"hello"s);
    }
    {
        std::error_code ec;
        auto            r = m::acp_to_wstring("hello"sv, ec);
        EXPECT_FALSE(ec);
        EXPECT_EQ(r, L"hello"s);
    }
}

TEST(AcpAPIs_Extended, acp_to_u8string_ec)
{
    {
        std::u8string   out;
        std::error_code ec;
        m::acp_to_u8string("hello"sv, out, ec);
        EXPECT_FALSE(ec);
        EXPECT_EQ(out, u8"hello"s);
    }
    {
        std::error_code ec;
        auto            r = m::acp_to_u8string("hello"sv, ec);
        EXPECT_FALSE(ec);
        EXPECT_EQ(r, u8"hello"s);
    }
}

TEST(AcpAPIs_Extended, acp_to_u16string_ec)
{
    {
        std::u16string  out;
        std::error_code ec;
        m::acp_to_u16string("hello"sv, out, ec);
        EXPECT_FALSE(ec);
        EXPECT_EQ(out, u"hello"s);
    }
    {
        std::error_code ec;
        auto            r = m::acp_to_u16string("hello"sv, ec);
        EXPECT_FALSE(ec);
        EXPECT_EQ(r, u"hello"s);
    }
}

TEST(AcpAPIs_Extended, acp_to_u32string_ec)
{
    {
        std::u32string  out;
        std::error_code ec;
        m::acp_to_u32string("hello"sv, out, ec);
        EXPECT_FALSE(ec);
        EXPECT_EQ(out, U"hello"s);
    }
    {
        std::error_code ec;
        auto            r = m::acp_to_u32string("hello"sv, ec);
        EXPECT_FALSE(ec);
        EXPECT_EQ(r, U"hello"s);
    }
}

// ---------------------------------------------------------------------------
// Optional overloads from <m/cp_acp/to_string.h>
// ---------------------------------------------------------------------------

// Nullopt propagation — void overloads

TEST(AcpAPIs_Extended, optional_nullopt_void_out)
{
    std::optional<std::string> in  = std::nullopt;
    std::optional<std::string> out = "sentinel"s;
    m::acp_to_string(in, out);
    EXPECT_FALSE(out.has_value());
}

TEST(AcpAPIs_Extended, optional_nullopt_void_out_ec)
{
    std::optional<std::string> in  = std::nullopt;
    std::optional<std::string> out = "sentinel"s;
    std::error_code            ec;
    m::acp_to_string(in, out, ec);
    EXPECT_FALSE(out.has_value());
    EXPECT_FALSE(ec);
}

// Nullopt propagation — value-return overloads

TEST(AcpAPIs_Extended, optional_nullopt_value_return)
{
    std::optional<std::string> in = std::nullopt;
    auto                       r  = m::acp_to_string(in);
    EXPECT_FALSE(r.has_value());
}

TEST(AcpAPIs_Extended, optional_nullopt_value_return_ec)
{
    std::optional<std::string> in = std::nullopt;
    std::error_code            ec;
    auto                       r = m::acp_to_string(in, ec);
    EXPECT_FALSE(r.has_value());
    EXPECT_FALSE(ec);
}

// Value present — void overloads

TEST(AcpAPIs_Extended, optional_value_void_out)
{
    std::optional<std::string> in  = "hello"s;
    std::optional<std::string> out = std::nullopt;
    m::acp_to_string(in, out);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out, "hello"s);
}

TEST(AcpAPIs_Extended, optional_value_void_out_ec)
{
    std::optional<std::string> in  = "world"s;
    std::optional<std::string> out = std::nullopt;
    std::error_code            ec;
    m::acp_to_string(in, out, ec);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out, "world"s);
    EXPECT_FALSE(ec);
}

// Value present — value-return overloads

TEST(AcpAPIs_Extended, optional_value_value_return)
{
    std::optional<std::string> in = "test"s;
    auto                       r  = m::acp_to_string(in);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, "test"s);
}

TEST(AcpAPIs_Extended, optional_value_value_return_ec)
{
    std::optional<std::string> in = "test"s;
    std::error_code            ec;
    auto                       r = m::acp_to_string(in, ec);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, "test"s);
    EXPECT_FALSE(ec);
}

// string_view as the optional's contained type

TEST(AcpAPIs_Extended, optional_string_view_value_return)
{
    std::optional<std::string_view> in = "viewdata"sv;
    auto                            r  = m::acp_to_string(in);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, "viewdata"s);
}

TEST(AcpAPIs_Extended, optional_string_view_nullopt_value_return)
{
    std::optional<std::string_view> in = std::nullopt;
    auto                            r  = m::acp_to_string(in);
    EXPECT_FALSE(r.has_value());
}
