// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <latch>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

#include <m/debugging/dbg_format.h>
#include <m/print/print.h>
#include <m/threadpool/threadpool.h>
#include <m/threadpool/work_item_state.h>
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
    auto wi = q->enqueue([] { m::println("Hello, world!"); });
    q->wait_for(5s);
}

TEST(WorkQueue, QueueWithDescriptions)
{
    auto                  q = m::threadpool->create_work_queue();
    constexpr std::size_t n = 5;

    std::array<std::shared_ptr<m::work_item>, n> work_items;
    for (std::size_t i = 0; i < work_items.size(); i++)
    {
        work_items[i] = q->enqueue(
            [x = i] { m::println("Hello there number {}", x); }, L"Work item number {}", i);
    }

    q->wait_for(5s);
}

TEST(WorkQueue, QueueN20)
{
    auto                                         q = m::threadpool->create_work_queue();
    constexpr std::size_t                        n = 20;
    std::array<std::shared_ptr<m::work_item>, n> work_items;

    for (std::size_t i = 0; i < n; i++)
    {
        work_items[i] = q->enqueue([x = i] { m::println("Hello there number {}", x); });
    }

    q->wait_for(5s);
}

TEST(WorkQueue, QueueNBig)
{
    auto                  q = m::threadpool->create_work_queue();
    constexpr std::size_t n = 100'000;

    auto work_items =
        std::unique_ptr<std::shared_ptr<m::work_item>[]>(new std::shared_ptr<m::work_item>[n]());
    auto flags_unique_ptr = std::unique_ptr<std::atomic<uint8_t>[]>(new std::atomic<uint8_t>[n]);
    auto flags            = flags_unique_ptr.get();

    auto const before_queue = m::clock_type::now();

    for (std::size_t i = 0; i < n; i++)
    {
        work_items[i] = q->enqueue([p = &flags[i]] { *p = 1; });
    }

    auto const after_queue = m::clock_type::now();

    constexpr auto d = 250ms;

    while (!q->wait_for(d))
        m::println("After {}, {} queue items still running", d, q->running());

    auto const after_wait = m::clock_type::now();

    q.reset();

    // Once the queue has drained, verify the flags are all set
    for (std::size_t i = 0; i < n; i++)
        EXPECT_EQ(flags[i], 1);

    m::println("It took {} to queue, and then {} for the work to finish",
               after_queue - before_queue,
               after_wait - after_queue);
}

// Heavy throughput stress run. Disabled by default so it is not part of the
// normal test pass (which should stay around 1-2 seconds); run explicitly with
// --gtest_also_run_disabled_tests when you want the big soak.
TEST(WorkQueue, DISABLED_QueueNBigStress)
{
    auto                  q = m::threadpool->create_work_queue();
    constexpr std::size_t n = 1'300'000;

    auto work_items =
        std::unique_ptr<std::shared_ptr<m::work_item>[]>(new std::shared_ptr<m::work_item>[n]());
    auto flags_unique_ptr = std::unique_ptr<std::atomic<uint8_t>[]>(new std::atomic<uint8_t>[n]);
    auto flags            = flags_unique_ptr.get();

    auto const before_queue = m::clock_type::now();

    for (std::size_t i = 0; i < n; i++)
    {
        work_items[i] = q->enqueue([p = &flags[i]] { *p = 1; });
    }

    auto const after_queue = m::clock_type::now();

    constexpr auto d = 250ms;

    while (!q->wait_for(d))
        m::println("After {}, {} queue items still running", d, q->running());

    auto const after_wait = m::clock_type::now();

    q.reset();

    // Once the queue has drained, verify the flags are all set
    for (std::size_t i = 0; i < n; i++)
        EXPECT_EQ(flags[i], 1);

    m::println("It took {} to queue, and then {} for the work to finish",
               after_queue - before_queue,
               after_wait - after_queue);
}
// ---------------------------------------------------------------------------
// queue_size() / running() baseline tests
// ---------------------------------------------------------------------------

TEST(WorkQueue, InitialQueueSizeIsZero)
{
    auto q = m::threadpool->create_work_queue();
    EXPECT_EQ(q->queue_size(), 0u);
}

TEST(WorkQueue, InitialRunningIsZero)
{
    auto q = m::threadpool->create_work_queue();
    EXPECT_EQ(q->running(), 0u);
}

TEST(WorkQueue, QueueAndRunningZeroAfterDrain)
{
    auto q = m::threadpool->create_work_queue();
    for (int i = 0; i < 10; ++i)
        static_cast<void>(q->enqueue([] {}));

    q->wait_for(5s);

    EXPECT_EQ(q->queue_size(), 0u);
    EXPECT_EQ(q->running(), 0u);
}

// ---------------------------------------------------------------------------
// work_item::id() / description() tests
// ---------------------------------------------------------------------------

TEST(WorkQueue, WorkItemIdIsNonZero)
{
    auto q  = m::threadpool->create_work_queue();
    auto wi = q->enqueue([] {});
    q->wait_for(5s);

    EXPECT_NE(wi->id(), 0u);
}

TEST(WorkQueue, WorkItemIdsAreUnique)
{
    auto q   = m::threadpool->create_work_queue();
    auto wi1 = q->enqueue([] {});
    auto wi2 = q->enqueue([] {});
    q->wait_for(5s);

    EXPECT_NE(wi1->id(), wi2->id());
}

TEST(WorkQueue, WorkItemDescriptionMatchesFormatString)
{
    auto q  = m::threadpool->create_work_queue();
    int  n  = 7;
    auto wi = q->enqueue([] {}, L"item number {}", n);
    q->wait_for(5s);

    EXPECT_EQ(std::wstring_view(wi->description()), L"item number 7");
}

TEST(WorkQueue, WorkItemEmptyDescriptionWhenNoneProvided)
{
    auto q  = m::threadpool->create_work_queue();
    auto wi = q->enqueue([] {});
    q->wait_for(5s);

    EXPECT_EQ(std::wstring_view(wi->description()), L"");
}

// ---------------------------------------------------------------------------
// work_item::state() test
// ---------------------------------------------------------------------------

TEST(WorkQueue, WorkItemStateProgressionQueuedRunningDone)
{
    // Use latches to observe the item while it is in the "running" state.
    std::latch started(2); // item signals it has begun; test thread consumes
    std::latch finish(1);  // test thread releases item to complete

    auto q  = m::threadpool->create_work_queue();
    auto wi = q->enqueue([&]() {
        started.arrive_and_wait(); // state is already "running" here
        finish.wait();
    });

    // Wait until the callback has begun (state must be running).
    started.arrive_and_wait();
    EXPECT_EQ(wi->state(), m::work_item_state::running);

    // Let the item finish and verify done state.
    finish.count_down();
    wi->wait();
    EXPECT_EQ(wi->state(), m::work_item_state::done);
}

// ---------------------------------------------------------------------------
// work_item timing tests
// ---------------------------------------------------------------------------

TEST(WorkQueue, WorkItemTimingsAreSetAndOrdered)
{
    auto q  = m::threadpool->create_work_queue();
    auto wi = q->enqueue([] { std::this_thread::sleep_for(5ms); });
    q->wait_for(5s);

    auto const start_time = wi->start_time();
    auto const end_time   = wi->end_time();

    EXPECT_TRUE(start_time.has_value()) << "start_time not set after completion";
    EXPECT_TRUE(end_time.has_value()) << "end_time not set after completion";

    EXPECT_LE(wi->enqueue_time(), *start_time);
    EXPECT_LE(*start_time, *end_time);
}

TEST(WorkQueue, WorkItemTimesStructMatchesIndividualAccessors)
{
    auto q  = m::threadpool->create_work_queue();
    auto wi = q->enqueue([] {});
    q->wait_for(5s);

    auto const t = wi->times();

    EXPECT_TRUE(t.m_start_time.has_value());
    EXPECT_TRUE(t.m_end_time.has_value());
    EXPECT_EQ(t.m_enqueue_time, wi->enqueue_time());
    EXPECT_EQ(t.m_start_time, wi->start_time());
    EXPECT_EQ(t.m_end_time, wi->end_time());
}

// ---------------------------------------------------------------------------
// work_item::wait() / wait_for() tests
// ---------------------------------------------------------------------------

TEST(WorkQueue, WorkItemWaitBlocksUntilDone)
{
    std::atomic<bool> done{false};

    auto q  = m::threadpool->create_work_queue();
    auto wi = q->enqueue([&]() {
        std::this_thread::sleep_for(20ms);
        done.store(true, std::memory_order_release);
    });

    wi->wait();

    EXPECT_TRUE(done.load(std::memory_order_acquire));
    EXPECT_EQ(wi->state(), m::work_item_state::done);
}

TEST(WorkQueue, WorkItemWaitForReturnsTrueWhenDone)
{
    auto q  = m::threadpool->create_work_queue();
    auto wi = q->enqueue([] {});

    EXPECT_TRUE(wi->wait_for(5s));
    EXPECT_EQ(wi->state(), m::work_item_state::done);
}

TEST(WorkQueue, WorkItemWaitForReturnsFalseOnTimeout)
{
    std::latch hold(1);

    auto q  = m::threadpool->create_work_queue();
    auto wi = q->enqueue([&]() { hold.wait(); }); // item blocks until released

    // wait_for with a very short duration should time out.
    EXPECT_FALSE(wi->wait_for(1ms));

    hold.count_down();
    wi->wait();
}

// ---------------------------------------------------------------------------
// work_item::try_cancel() test (documents current stub behaviour)
// ---------------------------------------------------------------------------

TEST(WorkQueue, WorkItemTryCancelCurrentlyAlwaysReturnsFalse)
{
    // try_cancel() is unimplemented and always returns false.
    std::latch hold(1);

    auto q  = m::threadpool->create_work_queue();
    auto wi = q->enqueue([&]() { hold.wait(); });

    // Cancel is attempted while item is enqueued or running.
    EXPECT_FALSE(wi->try_cancel());

    hold.count_down();
    wi->wait();
}

// ---------------------------------------------------------------------------
// work_queue::wait_for() return-value tests
// ---------------------------------------------------------------------------

TEST(WorkQueue, WaitForReturnsTrueWhenAllWorkCompletes)
{
    auto q   = m::threadpool->create_work_queue();
    auto wi1 = q->enqueue([] {});
    auto wi2 = q->enqueue([] {});

    EXPECT_TRUE(q->wait_for(5s));
    EXPECT_EQ(q->queue_size(), 0u);
    EXPECT_EQ(q->running(), 0u);
}

TEST(WorkQueue, WaitForReturnsFalseWhenWorkStillRunning)
{
    std::latch hold(1);

    auto q  = m::threadpool->create_work_queue();
    auto wi = q->enqueue([&]() { hold.wait(); }); // occupies the queue

    EXPECT_FALSE(q->wait_for(1ms));

    hold.count_down();
    q->wait_for(5s);
}

// ---------------------------------------------------------------------------
// Queue created with a description
// ---------------------------------------------------------------------------

TEST(WorkQueue, CreateWorkQueueWithDescription)
{
    int  n = 42;
    auto q = m::threadpool->create_work_queue(m::work_queue_execution_policy::parallel,
                                              L"TestQueue {}", n);
    EXPECT_NE(q, nullptr);

    auto wi = q->enqueue([] {});
    q->wait_for(5s);
}

// ---------------------------------------------------------------------------
// close() teardown tests
// ---------------------------------------------------------------------------

TEST(WorkQueue, CloseOnIdleQueueIsSafe)
{
    auto q = m::threadpool->create_work_queue();
    q->close();
}

TEST(WorkQueue, CloseAfterDrainIsSafe)
{
    auto q = m::threadpool->create_work_queue();
    for (int i = 0; i < 10; ++i)
        static_cast<void>(q->enqueue([] {}));

    EXPECT_TRUE(q->wait_for(5s));
    q->close();

    EXPECT_EQ(q->running(), 0u);
}

TEST(WorkQueue, CloseDrainsInFlightWork)
{
    std::latch started(1);
    std::latch release(1);

    auto q  = m::threadpool->create_work_queue();
    auto wi = q->enqueue([&]() {
        started.count_down();
        release.wait();
    });

    started.wait();    // ensure the callback is actually in flight
    release.count_down();

    q->close(); // must synchronously wait for the in-flight callback to finish

    EXPECT_EQ(q->running(), 0u);
}

TEST(WorkQueue, CloseIsIdempotent)
{
    auto q = m::threadpool->create_work_queue();
    static_cast<void>(q->enqueue([] {}));
    EXPECT_TRUE(q->wait_for(5s));

    q->close();
    q->close();
}

TEST(WorkQueue, DestroyWithoutCloseDrains)
{
    std::latch started(1);
    std::latch release(1);

    auto q = m::threadpool->create_work_queue();
    static_cast<void>(q->enqueue([&]() {
        started.count_down();
        release.wait();
    }));

    started.wait();
    release.count_down();

    // No explicit close(): the destructor must synchronously drain.
    q.reset();
}
