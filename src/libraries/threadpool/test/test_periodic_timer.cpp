// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <latch>
#include <span>
#include <string_view>

#include <m/debugging/dbg_format.h>
#include <m/threadpool/threadpool.h>

using namespace std::chrono_literals;
using namespace std::string_view_literals;

TEST(PeriodicTimer, BasicCreation)
{
    auto t1 = m::threadpool->create_periodic_timer([]() {});
}

TEST(PeriodicTimer, Test1)
{
    std::atomic<uintmax_t> counter;

    auto t1 = m::threadpool->create_periodic_timer([&]() { counter.fetch_add(1); });

    t1->set(100ms);

    std::this_thread::sleep_for(2s);

    EXPECT_GT(counter, 17);
    EXPECT_LT(counter, 22);
}
