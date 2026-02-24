// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <latch>
#include <span>
#include <string_view>
#include <thread>

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

// ---------------------------------------------------------------------------
// is_set() tests
// ---------------------------------------------------------------------------

TEST(PeriodicTimer, IsSetInitiallyFalse)
{
    auto t1 = m::threadpool->create_periodic_timer([]() {});
    EXPECT_FALSE(t1->is_set());
}

TEST(PeriodicTimer, IsSetTrueAfterSet)
{
    std::latch fired(1);

    auto t1 = m::threadpool->create_periodic_timer([&]() {
        fired.count_down(); // signal first firing
    });

    EXPECT_FALSE(t1->is_set());
    t1->set(100ms);
    EXPECT_TRUE(t1->is_set());

    // Wait for at least one callback so we know the timer is operating.
    fired.wait();

    // Still set (periodic timers keep firing until stopped).
    EXPECT_TRUE(t1->is_set());

    t1->stop();
    t1->wait();
}

// ---------------------------------------------------------------------------
// stop() regression tests (bug: m_set_count was never incremented)
// ---------------------------------------------------------------------------

TEST(PeriodicTimer, StopAfterSetSucceeds)
{
    // Regression: before the fix, do_stop() always threw because m_set_count
    // was never incremented in do_set(), making the "not started" guard always
    // true even on a running timer.
    auto t1 = m::threadpool->create_periodic_timer([]() {});
    t1->set(100ms);
    EXPECT_NO_THROW(t1->stop());
    t1->wait();
}

TEST(PeriodicTimer, IsSetFalseAfterStop)
{
    auto t1 = m::threadpool->create_periodic_timer([]() {});
    t1->set(100ms);
    t1->stop();
    EXPECT_FALSE(t1->is_set());
    t1->wait();
}

TEST(PeriodicTimer, WaitCompletesAfterStop)
{
    auto t1 = m::threadpool->create_periodic_timer([]() {});
    t1->set(100ms);
    t1->stop();
    t1->wait(); // must not hang
}

TEST(PeriodicTimer, DoubleStopThrows)
{
    // Stopping an already-stopped timer must throw.
    auto t1 = m::threadpool->create_periodic_timer([]() {});
    t1->set(100ms);
    t1->stop();
    t1->wait();
    EXPECT_ANY_THROW(t1->stop());
}

TEST(PeriodicTimer, StopPreventsFurtherFiring)
{
    std::atomic<uintmax_t> counter{};

    auto t1 = m::threadpool->create_periodic_timer([&]() { counter.fetch_add(1); });

    t1->set(50ms);
    std::this_thread::sleep_for(250ms); // let it fire a few times

    t1->stop();
    t1->wait();

    auto const count_at_stop = counter.load();
    EXPECT_GT(count_at_stop, 0u);

    // After stop + wait no further callbacks should arrive.
    std::this_thread::sleep_for(200ms);
    EXPECT_EQ(counter.load(), count_at_stop);
}

// ---------------------------------------------------------------------------
// Deadlock regression: callback must not hold internal mutex when invoked
// ---------------------------------------------------------------------------

TEST(PeriodicTimer, CallbackCanCallIsSetWithoutDeadlock)
{
    // Before the fix, on_tp_timer called user code while holding m_mutex.
    // do_is_set() on Windows delegates to ::IsThreadpoolTimerSet() (no m_mutex),
    // but the test demonstrates that is_set() is callable from the callback.
    std::latch checked(1);
    m::periodic_timer* timer_ptr = nullptr;

    auto t1 = m::threadpool->create_periodic_timer([&]() {
        if (timer_ptr != nullptr)
        {
            (void)timer_ptr->is_set();
            checked.count_down();
        }
    });

    timer_ptr = t1.get();
    t1->set(50ms);
    checked.wait(); // block until callback has run and checked is_set()

    t1->stop();
    t1->wait();
}

TEST(PeriodicTimer, StopCanBeCalledFromCallbackWithoutDeadlock)
{
    // Regression: on_tp_timer held m_mutex across the user callback.
    // stop() also takes m_mutex, so calling stop() from the callback would
    // deadlock.  After the fix the callback is invoked with the lock released.
    std::latch stopped(1);
    std::unique_ptr<m::periodic_timer> t1;

    t1 = m::threadpool->create_periodic_timer([&]() {
        // Stop from within the callback – must not deadlock.
        if (stopped.try_wait())
            return; // already stopped on a previous re-entry

        t1->stop();
        stopped.count_down();
    });

    t1->set(50ms);
    stopped.wait(); // block until callback has called stop()
    t1->wait();
}
