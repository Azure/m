// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include <m/cast/to.h>
#include <m/utility/to_underlying.h>

TEST(ToTests, UnScopedEnumTo)
{
    enum E1
    {
        e1,
        e2,
        e3,
    };

    E1 v1{e1};

    auto i1 = m::to<int>(v1);

    EXPECT_EQ(m::to_underlying(e1), i1);
}
