// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <chrono>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include <m/arefc_ptr/arefc_ptr.h>
#include <m/print/print.h>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

struct alignas(128) BiglyAlignedStruct
{
    int m_x;
};

TEST(TestRefCount, First)
{
    // auto p = m::mmake_arefc<std::string>("Hello there");

    //
}

TEST(TestRefCount, TryAlignedStruct)
{
    auto p = m::mmake_arefc<BiglyAlignedStruct>(10);

    EXPECT_EQ(p->m_x, 10);
}
