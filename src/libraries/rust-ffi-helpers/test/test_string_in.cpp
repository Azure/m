// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#ifdef WIN32
#include <m/windows_strings/convert.h>
#else
#include <m/linux_strings/convert.h>
#endif

#include <m/rust-ffi-helpers/string_in.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

template <std::size_t s1, std::size_t s2>
bool
spans_equal(std::span<uint8_t const, s1> const& span1, std::span<uint8_t const, s2> const& span2)
{
    if (span1.size() != span2.size())
        return false;

    for (std::size_t i = 0; i < span1.size(); i++)
        if (span1[i] != span2[i])
            return false;

    return true;
}

TEST(StringIn, VerifyStringViewW)
{
    auto u8string = u8"Hello, world"sv;
    auto u8span   = std::span<uint8_t const>(reinterpret_cast<uint8_t const*>(u8string.data()),
                                           u8string.size());

    m::rust::string_in si1(L"Hello, world"sv);
    auto               data = si1.data();
    auto               size = si1.size();
    auto               sispan = std::span(data, size);
    EXPECT_TRUE(spans_equal(u8span, sispan));

    EXPECT_EQ(size, 12);
}


TEST(StringIn, VerifyStringView)
{
    auto u8string = u8"Hello, world"sv;
    auto u8span   = std::span<uint8_t const>(reinterpret_cast<uint8_t const*>(u8string.data()),
                                           u8string.size());

    m::rust::string_in si1("Hello, world"sv);
    auto               data   = si1.data();
    auto               size   = si1.size();
    auto               sispan = std::span(data, size);
    EXPECT_TRUE(spans_equal(u8span, sispan));
}

