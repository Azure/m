// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/utility/compiler.h>

#include <chrono>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include <m/inplace_vector/inplace_vector.h>
#include <m/puddle/puddle.h>
#include <m/print/print.h>
#include <m/test_data/test_data.h>

using namespace std::chrono_literals;

TEST(TestPool, CreateACharPool)
{
    m::puddle<char, 512> p;
    //
    //
}

TEST(TestPool, CreateAInplaceVectorPool)
{
    auto p = std::make_shared<m::puddle<m::inplace_vector<char8_t, 1024>, 512>>();

    auto x = p->try_allocate();
    x.reset();
}

#if 0 // unsure why if-d out
TEST(TestPool, AllocateAndDeallocateChar)
{
    using puddle_type       = m::puddle<char, 8>;

    auto                         p = std::make_shared<puddle_type>();

    // Allocate all slots
    for (size_t i = 0; i < 8; ++i)
    {
        auto obj = p->try_allocate();
        ASSERT_NE(obj.get(), nullptr);
        *obj = static_cast<char>('A' + i);
        allocated.push_back(std::move(obj));
    }

    std::atomic<bool> allocating{false};

    // Pool should be exhausted now
    std::thread t([&p, &allocating]() {
        // Try to allocate in another thread, should block until deallocation
        allocating = true;
        allocating.notify_one();
        auto obj = p->allocate();
        ASSERT_NE(obj.get(), nullptr);
        *obj = 'Z';
    });

    allocating.wait(false);
    // Sleep to ensure thread is waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Deallocate one object to unblock thread
    allocated.back().reset();
    t.join();
}

TEST(TestPool, AllocateForTimeout)
{
    auto p = std::make_shared<m::puddle<int, 2>>();
    auto a = p->allocate();
    auto b = p->allocate();

    // Pool exhausted, should timeout
    auto result = p->allocate_for(std::chrono::milliseconds(20));
    ASSERT_FALSE(result.has_value());

    // Deallocate one, should succeed
    a.reset();
    result = p->allocate_for(std::chrono::milliseconds(20));
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(result.value().get(), nullptr);
}

TEST(TestPool, UniqueOwnership)
{
    auto puddle_ptr = std::make_shared<m::puddle<double, 4>>();
    auto obj1     = puddle_ptr->allocate();
    auto obj2     = puddle_ptr->allocate();

    *obj1 = 3.14;
    *obj2 = 2.71;

    ASSERT_DOUBLE_EQ(*obj1, 3.14);
    ASSERT_DOUBLE_EQ(*obj2, 2.71);

    obj1.reset();
    obj2.reset();

    // After reset, should be able to allocate again
    auto obj3 = puddle_ptr->allocate();
    ASSERT_NE(obj3.get(), nullptr);
}

TEST(TestPool, PoolExhaustionBlocks)
{
    auto p   = std::make_shared<m::puddle<int, 1>>();
    auto obj = p->allocate();

    std::atomic<bool> readyToStart{false};
    std::atomic<bool> goAhead{false};

    std::chrono::milliseconds waitDuration{};

    std::thread t([&]() {
        readyToStart = true;
        readyToStart.notify_one();

        goAhead.wait(false);

        auto const beginTP = std::chrono::utc_clock::now();

        auto obj2 = p->allocate();

        auto const endTP = std::chrono::utc_clock::now();

        waitDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTP - beginTP);
    });

    readyToStart.wait(false);

    goAhead = true;
    goAhead.notify_one();

    std::chrono::milliseconds sleepTime = 50ms;

    std::this_thread::sleep_for(sleepTime);

    obj.reset();

    t.join();

    // Make sure that the wait time was something around what
    // the wait time was
    ASSERT_GT(waitDuration.count(), static_cast<double>(sleepTime.count()) * 0.8);
}

TEST(TestPool, PoolCount)
{
    auto const x = m::puddle_size_v<int, 1>;
    ASSERT_GT(x, 0);
}
#endif
