// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <latch>
#include <print>
#include <span>
#include <string_view>

#include <m/debugging/dbg_format.h>
#include <m/threadpool/threadpool.h>
#include <m/threadpool/work_queue.h>

using namespace std::chrono_literals;
using namespace std::string_view_literals;

TEST(WorkQueue, BasicCreation)
{
    auto q = m::threadpool->create_work_queue(m::work_queue_execution_policy::parallel);

    EXPECT_NE(q, nullptr);
}

TEST(WorkQueue, Queue1)
{
    auto q  = m::threadpool->create_work_queue();
    auto wi = q->enqueue<void>([] { std::println("Hello, world!"); });
    q->wait_for(5s);
}

TEST(WorkQueue, QueueN20)
{
    auto                                         q = m::threadpool->create_work_queue();
    constexpr std::size_t                        n = 20;
    std::array<std::shared_ptr<m::work_item>, n> work_items;

    for (std::size_t i = 0; i < n; i++)
    {
        work_items[i] = q->enqueue<void>([x = i] { std::println("Hello there number {}", x); });
    }

    q->wait_for(5s);
}

TEST(WorkQueue, QueueNBig)
{
    auto                  q = m::threadpool->create_work_queue();
    constexpr std::size_t n = 1'300'000;

    auto work_items =
        std::unique_ptr<std::shared_ptr<m::work_item>[]>(new std::shared_ptr<m::work_item>[n]());
    auto flags_unique_ptr = std::unique_ptr<std::atomic<uint8_t>[]>(new std::atomic<uint8_t>[n]);
    auto flags            = flags_unique_ptr.get();

    auto const before_queue = m::clock::now();

    for (std::size_t i = 0; i < n; i++)
    {
        work_items[i] = q->enqueue<void>([p = &flags[i]] { *p = 1; });
    }

    auto const after_queue = m::clock::now();

    constexpr auto d = 250ms;

    while (!q->wait_for(d))
        std::println("After {}, {} queue items still running", d, q->running());

    auto const after_wait = m::clock::now();

    q.reset();

    // Once the queue has drained, verify the flags are all set
    for (std::size_t i = 0; i < n; i++)
        EXPECT_EQ(flags[i], 1);

    std::println("It took {} to queue, and then {} for the work to finish",
                 after_queue - before_queue,
                 after_wait - after_queue);
}
