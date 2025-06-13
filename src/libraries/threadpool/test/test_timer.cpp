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

TEST(Timer, BasicCreation)
{
    auto t1 = m::threadpool->create_timer([]() {});
}

TEST(Timer, RunImmediately)
{
    std::atomic<bool> ran{false};

    auto t1 = m::threadpool->create_timer([&]()
        {
        ran.store(true, std::memory_order_release);
        ran.notify_all();
    });

    EXPECT_EQ(ran, false);

    t1->set(0s);

    ran.wait(false, std::memory_order_acquire);
}

TEST(Timer, Wait100ms)
{
    std::atomic<bool>                     done{false};
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;

    auto t1 = m::threadpool->create_timer([&]() {
        m::dbg_format(L"Inside the Wait100ms timer lambda\n");
        end_time = std::chrono::system_clock::now();
        done.store(true, std::memory_order_release);
        done.notify_all();
    });

    EXPECT_EQ(done, false);

    constexpr auto wait_time = 100ms;

    start_time = std::chrono::system_clock::now();
    t1->set(wait_time);

    done.wait(false, std::memory_order_acquire);

    auto actual_time = end_time - start_time;

    // Super hard to determine what the acceptable "slop time" is around the requested wait time vs.
    // the actual wait time.

    auto ref_dur = std::chrono::duration_cast<std::chrono::microseconds>(wait_time);
    auto act_dur = std::chrono::duration_cast<std::chrono::microseconds>(actual_time);

    intmax_t refc = ref_dur.count();
    if (refc != 0)
    {
        intmax_t actc = act_dur.count();

        intmax_t delta = 0;

        if (refc > actc)
            delta = refc - actc;
        else
            delta = actc - refc;

        auto deltaratio = static_cast<double>(delta) / static_cast<double>(refc);

        // Seems like a less than 20% miss is a reasonable expectation.
        EXPECT_LT(deltaratio, 0.20) << "Reference duration was: " << ref_dur << " actual duration waited was: " << act_dur
            << " [refc = " << refc << "; actc = " << actc << "; delta = " << delta << "; deltaratio = " << deltaratio << "]" << std::endl;
    }

}


TEST(Timer, Wait100msTwice)
{
    std::atomic<bool>                     done{false};
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;

    auto t1 = m::threadpool->create_timer([&]() {
        m::dbg_format(L"Inside the Wait100ms timer lambda\n");
        end_time = std::chrono::system_clock::now();
        done.store(true, std::memory_order_release);
        done.notify_all();
    });

    EXPECT_EQ(done, false);

    constexpr auto wait_time = 100ms;

    start_time = std::chrono::system_clock::now();
    t1->set(wait_time);

    done.wait(false, std::memory_order_acquire);

    auto actual_time = end_time - start_time;

    // Super hard to determine what the acceptable "slop time" is around the requested wait time vs.
    // the actual wait time.

    auto ref_dur = std::chrono::duration_cast<std::chrono::microseconds>(wait_time);
    auto act_dur = std::chrono::duration_cast<std::chrono::microseconds>(actual_time);

    intmax_t refc = ref_dur.count();
    if (refc != 0)
    {
        intmax_t actc = act_dur.count();

        intmax_t delta = 0;

        if (refc > actc)
            delta = refc - actc;
        else
            delta = actc - refc;

        auto deltaratio = static_cast<double>(delta) / static_cast<double>(refc);

        // Seems like a less than 20% miss is a reasonable expectation.
        EXPECT_LT(deltaratio, 0.20)
            << "Reference duration was: " << ref_dur << " actual duration waited was: " << act_dur
            << " [refc = " << refc << "; actc = " << actc << "; delta = " << delta
            << "; deltaratio = " << deltaratio << "]" << std::endl;
    }

    // And basically just do it over again!

    done.store(false, std::memory_order_release);
    start_time = std::chrono::system_clock::now();
    t1->set(wait_time);

    done.wait(false, std::memory_order_acquire);

    actual_time = end_time - start_time;

    // Super hard to determine what the acceptable "slop time" is around the requested wait time vs.
    // the actual wait time.

    ref_dur = std::chrono::duration_cast<std::chrono::microseconds>(wait_time);
    act_dur = std::chrono::duration_cast<std::chrono::microseconds>(actual_time);

    refc = ref_dur.count();
    if (refc != 0)
    {
        intmax_t actc = act_dur.count();

        intmax_t delta = 0;

        if (refc > actc)
            delta = refc - actc;
        else
            delta = actc - refc;

        auto deltaratio = static_cast<double>(delta) / static_cast<double>(refc);

        // Seems like a less than 20% miss is a reasonable expectation.
        EXPECT_LT(deltaratio, 0.20)
            << "Reference duration was: " << ref_dur << " actual duration waited was: " << act_dur
            << " [refc = " << refc << "; actc = " << actc << "; delta = " << delta
            << "; deltaratio = " << deltaratio << "]" << std::endl;
    }
}

TEST(Timer, EnsureDestructionDelayed)
{
    std::latch        started(2);
    std::atomic<bool> ran{false};

    auto t1 = m::threadpool->create_timer([&]() {
        started.arrive_and_wait();
        std::this_thread::sleep_for(250ms);
        ran.store(true, std::memory_order_release);
        ran.notify_all();
    });

    EXPECT_EQ(ran, false);
    t1->set(0s);
    EXPECT_EQ(ran, false);
    // Wait until we know that the other thread is in the timer before 
    // we release the last reference on the smart pointer otherwise
    // we could release the pointer before the timer even launches.
    started.arrive_and_wait();
    t1.reset();
    EXPECT_EQ(ran, true);
}

