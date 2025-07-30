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

//
// The max delta ratio is a funny thing.
// 
// Timers generally give a guarantee of NOT waking before a certain time. The
// operating system's scheduler is in charge of how soon after the wake time
// a given thread is scheduled to run.
// 
// For timers, the only really fatal condition would be if the worker started
// before the elapsed time.
// 
// But we want to have some checks that the math for converting the
// std::chrono based durations to the time format used by the operating system
// hasn't gone completely wonky.
// 
// Experience shows that it's not uncommon to see as much as a 25ms delay in
// delivering a 100ms timer on a lightly loaded system. Is this good because
// it's saving power? Bad because it's not starting and leaving the system
// idle? These are policy level questions and beyond the scope of a general
// answer or unit test.
// 
// In lieu of moving the chrono-to-OS-times work to a whole separate
// component with another set of tests, we will have a relatively sloppy
// set of metrics here.
// 
// For some timer tests, the difference between the actual amount of delay
// and the requested amount of delay is computed. the ratio between the
// delta and the actual, we're calling the "delta ratio".
// 
// It was capped at 0.2 but that was too low on some systems. Late June
// 2025, I'm raising it to 0.5. That seems excessive but as mentioned
// this is to prevent egregious regressions in the conversion logic, not
// to double check the operating system scheduler.
// 
static constexpr double max_deltaratio = 0.5;

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

    EXPECT_FALSE(ran);

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

    EXPECT_FALSE(done);

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

        EXPECT_LT(deltaratio, max_deltaratio)
            << "Reference duration was: " << ref_dur << " actual duration waited was: " << act_dur
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

    EXPECT_FALSE(done);

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

        EXPECT_LT(deltaratio, max_deltaratio)
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

        EXPECT_LT(deltaratio, max_deltaratio)
            << "Reference duration was: " << ref_dur << " actual duration waited was: " << act_dur
            << " [refc = " << refc << "; actc = " << actc << "; delta = " << delta
            << "; deltaratio = " << deltaratio << "]" << std::endl;
    }
}

TEST(Timer, Wait100msTwiceWithDescription)
{
    std::atomic<bool>                     done{false};
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;

    auto t1 = m::threadpool->create_timer([&]() {
        m::dbg_format(L"Inside the Wait100ms timer lambda\n");
        end_time = std::chrono::system_clock::now();
        done.store(true, std::memory_order_release);
        done.notify_all();
        },
        L"This is just some description for a timer");

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

        EXPECT_LT(deltaratio, max_deltaratio)
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

        EXPECT_LT(deltaratio, max_deltaratio)
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

    EXPECT_FALSE(ran);
    t1->set(0s);
    EXPECT_FALSE(ran);
    // Wait until we know that the other thread is in the timer before 
    // we release the last reference on the smart pointer otherwise
    // we could release the pointer before the timer even launches.
    started.arrive_and_wait();
    t1.reset();
    EXPECT_TRUE(ran);
}

