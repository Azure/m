// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <array>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#include <m/strings/convert.h>
#include <m/utf/exceptions.h>
#include <m/utf/encode.h>

#include "test_data.h"

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace
{
    void
    test_encode(utf_data_set const& data)
    {
        std::u32string buffer;

        auto it = std::back_inserter(buffer);

        for (auto&& ch: data.m_u32_sv)
            it = m::utf::encode_utf32(char32_t{}, ch, it);

        EXPECT_EQ(data.m_u32_sv.compare(buffer), 0);
    }
} // namespace

TEST(Utf32Encoding, Hello) { test_encode(hellodata); }

TEST(Utf32Encoding, rfc3629_ex_1) { test_encode(rfc3629_ex_1); }

TEST(Utf32Encoding, rfc3629_ex_2) { test_encode(rfc3629_ex_2); }

TEST(Utf32Encoding, rfc3629_ex_3) { test_encode(rfc3629_ex_3); }

TEST(Utf32Encoding, rfc3629_ex_4) { test_encode(rfc3629_ex_4); }
