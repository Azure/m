// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <filesystem>
#include <span>
#include <string_view>

#include <m/utility/pointers.h>

using namespace std::string_view_literals;

struct S1
{
    int x;
};

struct S2
{
    int y;
};

struct S3 : S1
{
    int z;
};

TEST(UtilityPointers, first)
{
    S1 s1;
    S3 s3;

    auto p1 = m::not_null(&s1);
    auto p3 = m::not_null(&s3);
    p1      = p3;

    EXPECT_EQ(p1, &s3);
}
