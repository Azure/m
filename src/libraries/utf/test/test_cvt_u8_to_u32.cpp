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

#include "test_data.h"

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(CvtU8ToU32, TestZeroLength)
{
    auto x = m::to_u32string(std::u8string_view());

    EXPECT_EQ(x.size(), 0);
}

TEST(CvtU8ToU32, TestBasic)
{
    auto x = m::to_u32string(hellodata.m_u8sv);

    EXPECT_EQ(hellodata.m_u32sv.compare(x), 0);
}

#if 0
TEST(CvtU8ToU32, TestRFC3629_Example_1)
{
    auto x = m::to_u32string(u8_rfc3629_example_1_sv);

    EXPECT_EQ(u32_rfc3629_example_1_sv.compare(x), 0);
    //
}
#endif

