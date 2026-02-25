// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <atomic>

#include <Windows.h>

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
    constexpr int N = 10;
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
