// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//
// Teardown harness for the tracing monitor.
//
// The production monitor is a process-lifetime singleton that is normally only
// destroyed by CRT static destructors at process exit / DLL_PROCESS_DETACH,
// which is awkward to drive from a unit test. These tests instead build
// standalone monitors through the public make_monitor_class() factory so the
// exact same construction/teardown path can be exercised on demand.
//
// What we are demonstrating:
//   1. A create / use / destroy cycle leaks nothing (CRT debug heap diff == 0).
//   2. After a multithreaded burst of logging is fully quiesced (threads joined),
//      teardown still frees everything cleanly.
//   3. The destructor's "all message slots returned" tripwire actually fires
//      when a sink retains a message past teardown.
//

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

#include <m/tracing/close_flush_option.h>
#include <m/tracing/envelope.h>
#include <m/tracing/event_kind.h>
#include <m/tracing/may_forward_message_option.h>
#include <m/tracing/monitor_class.h>
#include <m/tracing/on_message_disposition.h>
#include <m/tracing/sink.h>
#include <m/tracing/sink_registration.h>
#include <m/tracing/source.h>
#include <m/tracing/tracing.h>

#if defined(_WIN32) && defined(_DEBUG)
#define M_TRACING_CRT_LEAK_CHECK 1
#include <crtdbg.h>
#else
#define M_TRACING_CRT_LEAK_CHECK 0
#endif

using namespace m::string_view_literals;

namespace
{
    // A minimal sink that processes (and discards) every message without ever
    // retaining ownership. This is the well-behaved case: every message slot is
    // returned to the queue as soon as the log call unwinds.
    class counting_sink : public m::tracing::sink
    {
    public:
        explicit counting_sink(m::not_null<m::tracing::monitor_class*> monitor):
            sink(L"counting_sink"_sl, monitor)
        {}

        ~counting_sink() override = default;

        std::uint64_t
        count() const
        {
            return m_count.load(std::memory_order_relaxed);
        }

    protected:
        m::tracing::on_message_disposition
        on_message(m::tracing::may_forward_message_option, m::tracing::envelope&) override
        {
            m_count.fetch_add(1, std::memory_order_relaxed);
            return m::tracing::on_message_disposition::message_processed;
        }

        bool
        could_forward_message(m::tracing::envelope const&) override
        {
            return false;
        }

        void
        close(m::tracing::close_flush_option) noexcept override
        {}

    private:
        std::atomic<std::uint64_t> m_count{0};
    };

    // A deliberately misbehaving sink that steals the envelope (taking ownership
    // of the message slot) and reports the message as forwarded, so the message
    // allocator does not return the slot to the queue. This models an async
    // forwarding sink that drops a message on the floor, which must trip the
    // monitor's teardown invariant.
    class retaining_sink : public m::tracing::sink
    {
    public:
        explicit retaining_sink(m::not_null<m::tracing::monitor_class*> monitor):
            sink(L"retaining_sink"_sl, monitor)
        {}

        ~retaining_sink() override = default;

    protected:
        m::tracing::on_message_disposition
        on_message(m::tracing::may_forward_message_option, m::tracing::envelope& env) override
        {
            auto l = std::unique_lock(m_mutex);
            m_held.emplace_back(std::move(env));
            return m::tracing::on_message_disposition::message_forwarded;
        }

        bool
        could_forward_message(m::tracing::envelope const&) override
        {
            return true;
        }

        void
        close(m::tracing::close_flush_option) noexcept override
        {}

    private:
        std::vector<m::tracing::envelope> m_held;
    };

    // One full create / register / log / destroy cycle against a standalone
    // monitor. Everything is scoped so it is fully torn down on return.
    void
    exercise_monitor_lifecycle(int message_count)
    {
        auto monitor = m::tracing::make_monitor_class();

        auto snk = std::make_shared<counting_sink>(monitor.get());
        auto reg = monitor->register_sink(m::tracing::diagnostic_channel_name, snk);

        auto src = monitor->make_source(m::tracing::event_kind::verbose);

        for (int i = 0; i < message_count; i++)
            src->wlog(m::tracing::event_kind::information, L"teardown harness message {}", i);
    }
} // namespace

// Demonstrates that a single-threaded create/use/destroy cycle leaks nothing.
TEST(TracingMonitorTeardown, SingleThreadCycleLeaksNothing)
{
    // Warm up once so any one-time/global allocations (lazy statics, TLS, first
    // pool growth) happen before we take the baseline checkpoint.
    exercise_monitor_lifecycle(8);

#if M_TRACING_CRT_LEAK_CHECK
    _CrtMemState before{};
    _CrtMemState after{};
    _CrtMemState diff{};

    _CrtMemCheckpoint(&before);
    exercise_monitor_lifecycle(32);
    _CrtMemCheckpoint(&after);

    // Capture the comparison without any intervening allocations, then assert.
    int const leaked = _CrtMemDifference(&diff, &before, &after);
    EXPECT_EQ(0, leaked) << "monitor teardown leaked heap blocks";
#else
    // Without the CRT debug heap we can still exercise the path; a leak or
    // double-free would surface under an external tool (e.g. ASan).
    exercise_monitor_lifecycle(32);
#endif
}

// Demonstrates that after a concurrent burst of logging is fully quiesced
// (all worker threads joined), the monitor tears down cleanly with no leak and
// without tripping the teardown invariant.
TEST(TracingMonitorTeardown, QuiesceThenTeardownMultiThread)
{
    auto run_burst = [] {
        auto monitor = m::tracing::make_monitor_class();

        auto snk = std::make_shared<counting_sink>(monitor.get());
        auto reg = monitor->register_sink(m::tracing::diagnostic_channel_name, snk);

        constexpr int thread_count          = 8;
        constexpr int messages_per_thread   = 200;
        std::atomic<bool> go{false};

        std::vector<std::thread> workers;
        workers.reserve(thread_count);

        for (int t = 0; t < thread_count; t++)
        {
            workers.emplace_back([&monitor, &go] {
                while (!go.load(std::memory_order_acquire))
                    std::this_thread::yield();

                auto src = monitor->make_source(m::tracing::event_kind::verbose);
                for (int i = 0; i < messages_per_thread; i++)
                    src->wlog(m::tracing::event_kind::information, L"burst {}", i);
            });
        }

        go.store(true, std::memory_order_release);

        for (auto&& w: workers)
            w.join();

        // All dispatch is now quiesced; monitor is destroyed on return.
    };

    // Warm up, then measure.
    run_burst();

#if M_TRACING_CRT_LEAK_CHECK
    _CrtMemState before{};
    _CrtMemState after{};
    _CrtMemState diff{};

    _CrtMemCheckpoint(&before);
    run_burst();
    _CrtMemCheckpoint(&after);

    int const leaked = _CrtMemDifference(&diff, &before, &after);
    EXPECT_EQ(0, leaked) << "multithreaded monitor teardown leaked heap blocks";
#else
    run_burst();
#endif
}

// Demonstrates that the teardown invariant fires when a message slot is still
// checked out (retained by a sink) at the time the monitor is destroyed.
TEST(TracingMonitorTeardownDeathTest, RetainedMessageTripsInvariant)
{
    EXPECT_DEATH(
        {
            auto monitor = m::tracing::make_monitor_class();

            auto snk = std::make_shared<retaining_sink>(monitor.get());
            auto reg = monitor->register_sink(m::tracing::diagnostic_channel_name, snk);

            auto src = monitor->make_source(m::tracing::event_kind::verbose);

            // The retaining sink steals this message's slot and never returns it,
            // so destroying the monitor must trip the "all slots returned" check.
            src->wlog(m::tracing::event_kind::information, L"steal this slot");
        },
        ".*");
}
