// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>

#include <Windows.h>

#include <m/win32/event.h>
#include <m/win32/threadpool.h>

TEST(Win32Threadpool, TpWorkSubmitAndWait)
{
    std::atomic<int> counter{0};

    auto callback = [](PTP_CALLBACK_INSTANCE, PVOID pv, PTP_WORK) noexcept {
        auto* p = static_cast<std::atomic<int>*>(pv);
        p->fetch_add(1, std::memory_order_relaxed);
    };

    m::win32::threadpool::tp_work work(callback, &counter);
    work.submit();
    work.wait_for_callbacks(false);

    EXPECT_EQ(counter.load(), 1);
}

TEST(Win32Threadpool, TpWorkSubmitMultiple)
{
    constexpr int    N = 10;
    std::atomic<int> counter{0};

    auto callback = [](PTP_CALLBACK_INSTANCE, PVOID pv, PTP_WORK) noexcept {
        auto* p = static_cast<std::atomic<int>*>(pv);
        p->fetch_add(1, std::memory_order_relaxed);
    };

    m::win32::threadpool::tp_work work(callback, &counter);
    for (int i = 0; i < N; ++i)
        work.submit();
    work.wait_for_callbacks(false);

    EXPECT_EQ(counter.load(), N);
}

TEST(Win32Threadpool, TpWorkMoveConstruct)
{
    std::atomic<int> counter{0};

    auto callback = [](PTP_CALLBACK_INSTANCE, PVOID pv, PTP_WORK) noexcept {
        static_cast<std::atomic<int>*>(pv)->fetch_add(1, std::memory_order_relaxed);
    };

    m::win32::threadpool::tp_work a(callback, &counter);
    m::win32::threadpool::tp_work b(std::move(a));

    EXPECT_FALSE(static_cast<bool>(a));
    EXPECT_TRUE(static_cast<bool>(b));

    b.submit();
    b.wait_for_callbacks(false);
    EXPECT_EQ(counter.load(), 1);
}

TEST(Win32Threadpool, TpWorkMoveAssign)
{
    std::atomic<int> counter{0};

    auto callback = [](PTP_CALLBACK_INSTANCE, PVOID pv, PTP_WORK) noexcept {
        static_cast<std::atomic<int>*>(pv)->fetch_add(1, std::memory_order_relaxed);
    };

    m::win32::threadpool::tp_work a(callback, &counter);
    m::win32::threadpool::tp_work b;

    EXPECT_FALSE(static_cast<bool>(b));
    b = std::move(a);
    EXPECT_FALSE(static_cast<bool>(a));
    EXPECT_TRUE(static_cast<bool>(b));

    b.submit();
    b.wait_for_callbacks(false);
    EXPECT_EQ(counter.load(), 1);
}

TEST(Win32Threadpool, TpWorkReset)
{
    std::atomic<int> counter{0};

    auto callback = [](PTP_CALLBACK_INSTANCE, PVOID pv, PTP_WORK) noexcept {
        static_cast<std::atomic<int>*>(pv)->fetch_add(1, std::memory_order_relaxed);
    };

    m::win32::threadpool::tp_work work(callback, &counter);
    EXPECT_TRUE(static_cast<bool>(work));

    work.reset();
    EXPECT_FALSE(static_cast<bool>(work));
}

TEST(Win32Threadpool, TpTimerConstructAndIsSet)
{
    auto callback = [](PTP_CALLBACK_INSTANCE, PVOID, PTP_TIMER) noexcept {};

    m::win32::threadpool::tp_timer timer(callback);
    EXPECT_TRUE(static_cast<bool>(timer));

    // Timer was never armed, so is_set() should be false.
    EXPECT_FALSE(timer.is_set());
}

TEST(Win32Threadpool, TpTimerSetAndCancel)
{
    std::atomic<bool> fired{false};

    auto callback = [](PTP_CALLBACK_INSTANCE, PVOID pv, PTP_TIMER) noexcept {
        static_cast<std::atomic<bool>*>(pv)->store(true, std::memory_order_relaxed);
    };

    m::win32::threadpool::tp_timer timer(callback, &fired);
    EXPECT_FALSE(timer.is_set());

    // Arm the timer 60 seconds in the future (absolute UTC FILETIME) so that
    // cancel() wins the race without any timing sensitivity.
    FILETIME due{};
    ::GetSystemTimeAsFileTime(&due);
    ULONGLONG ticks = (static_cast<ULONGLONG>(due.dwHighDateTime) << 32) | due.dwLowDateTime;
    ticks += 600'000'000ULL; // 60 s in 100-ns units
    due.dwLowDateTime  = static_cast<DWORD>(ticks & 0xFFFF'FFFFUL);
    due.dwHighDateTime = static_cast<DWORD>(ticks >> 32);

    timer.set(due);
    EXPECT_TRUE(timer.is_set());

    timer.cancel();
    EXPECT_FALSE(timer.is_set());
}

TEST(Win32Threadpool, TpTimerFiresAndWait)
{
    // Use a manual-reset event for deterministic synchronization: the callback
    // signals it, the main thread waits on it with a generous timeout.
    auto done = m::win32::event(m::win32::create_event_flags::manual_reset);

    auto callback = [](PTP_CALLBACK_INSTANCE, PVOID pv, PTP_TIMER) noexcept {
        ::SetEvent(static_cast<HANDLE>(*static_cast<m::win32::event*>(pv)));
    };

    m::win32::threadpool::tp_timer timer(callback, &done);

    // A zero FILETIME is January 1, 1601 — strictly in the past — so the
    // timer fires immediately.
    FILETIME due{};
    timer.set(due);

    EXPECT_EQ(::WaitForSingleObject(done.get(), 5000), WAIT_OBJECT_0);
}

TEST(Win32Threadpool, TpWaitConstructAndSetWait)
{
    // "trigger" is the event the waiter watches. "done" is signalled by the
    // callback so the main thread can wait deterministically.
    auto trigger = m::win32::event(
        m::win32::create_event_flags::initial_set | m::win32::create_event_flags::manual_reset);
    auto done = m::win32::event(m::win32::create_event_flags::manual_reset);

    struct ctx_t
    {
        m::win32::event* done;
    } ctx{&done};

    auto callback = [](PTP_CALLBACK_INSTANCE, PVOID pv, PTP_WAIT, TP_WAIT_RESULT) noexcept {
        auto* c = static_cast<ctx_t*>(pv);
        ::SetEvent(static_cast<HANDLE>(*c->done));
    };

    m::win32::threadpool::tp_wait waiter(callback, &ctx);
    // trigger is already signalled, so the callback fires immediately.
    waiter.set_wait(trigger);

    EXPECT_EQ(::WaitForSingleObject(done.get(), 5000), WAIT_OBJECT_0);
}
