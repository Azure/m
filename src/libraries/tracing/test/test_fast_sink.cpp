// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <chrono>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include <m/tracing/tracing.h>

using namespace std::chrono_literals;

namespace
{
    class fast_sink : public m::tracing::sink
    {
    public:
        fast_sink(m::not_null<m::tracing::monitor_class*> monitor):
            sink(L"cout_sink"_sl, monitor)
        {}

        virtual ~fast_sink() {}

        // Kind of hokey but who is responsible for registering the
        // cout based sink? This is how it's done I guess
        static std::unique_ptr<m::tracing::sink_registration>
        register_sink(m::not_null<m::tracing::monitor_class*> monitor)
        {
            std::shared_ptr<fast_sink> expected = ms_fast_sink.load(std::memory_order_acquire);

            if (!expected)
            {
                for (;;)
                {
                    auto desired = std::make_shared<fast_sink>(monitor);

                    if (ms_fast_sink.compare_exchange_strong(
                            expected, desired, std::memory_order_acq_rel))
                        break;
                }

                expected = ms_fast_sink.load(std::memory_order_acquire);
            }

            return monitor->register_sink(expected);
        }

    protected:
        m::tracing::on_message_disposition
        on_message(m::tracing::may_queue_option, m::tracing::envelope&) override
        {
            return m::tracing::on_message_disposition::completed;
        }

        bool
        would_queue(m::tracing::envelope const&) override
        {
            // false means that we will not queue (meaning take ownership of)
            // the messages during on_message().
            return false;
        }

        void
        close() noexcept override
        {
            {
                auto l = std::unique_lock(m_mutex);
                m_closed = true;
            }
        }

    private:
        static inline std::atomic<std::shared_ptr<fast_sink>> ms_fast_sink;
    };
} // namespace

TEST(TestFastSink, RegisterSink)
{
    auto fastsink = fast_sink::register_sink(m::tracing::monitor);
}

TEST(TestFastSink, LogAnEventNoFormattingWithConsoleSink)
{
    auto fastsink = fast_sink::register_sink(m::tracing::monitor.get());

    auto src = m::tracing::monitor->make_source();

    src->log(m::tracing::event_kind::information, "Hello, tracing!");
}

TEST(TestFastSink, LogATracingEventNoFormattingWithConsoleSink)
{
    auto fastsink = fast_sink::register_sink(m::tracing::monitor.get());

    auto src = m::tracing::monitor->make_source();

    src->log(m::tracing::event_kind::tracing, "Hello, tracing this should not show up!");
}

TEST(TestFastSink, LogAErrorEventNoFormattingWithConsoleSink)
{
    auto fastsink = fast_sink::register_sink(m::tracing::monitor.get());

    auto src = m::tracing::monitor->make_source();

    src->log(m::tracing::event_kind::error, "Hello, tracing this should definitely show up!");
}

TEST(TestFastSink, WLogAnEventNoFormattingWithConsoleSink)
{
    auto fastsink = fast_sink::register_sink(m::tracing::monitor.get());

    auto src = m::tracing::monitor->make_source();

    src->wlog(m::tracing::event_kind::information, L"Hello, tracing!");
}

TEST(TestFastSink, WLogATracingEventNoFormattingWithConsoleSink)
{
    auto fastsink = fast_sink::register_sink(m::tracing::monitor.get());

    auto src = m::tracing::monitor->make_source();

    src->wlog(m::tracing::event_kind::tracing, L"Hello, tracing this should not show up!");
}

TEST(TestFastSink, WLogAErrorEventNoFormattingWithConsoleSink)
{
    auto fastsink = fast_sink::register_sink(m::tracing::monitor.get());

    auto src = m::tracing::monitor->make_source();

    src->wlog(m::tracing::event_kind::error, L"Hello, tracing this should definitely show up!");
}

TEST(TestFastSink, LogMessagesAfterClosingSink)
{
    auto fastsink = fast_sink::register_sink(m::tracing::monitor.get());
    auto src      = m::tracing::monitor->make_source();

    src->wlog(m::tracing::event_kind::error, L"Hello, tracing this should definitely show up!");
    fastsink.reset();
    src->wlog(m::tracing::event_kind::error,
              L"This is another event but after the sink was closed");
}

TEST(TestFastSink, LotsOfMessages10)
{
    auto fastsink = fast_sink::register_sink(m::tracing::monitor.get());
    auto src      = m::tracing::monitor->make_source();

    constexpr auto message_count = 10;

    for (auto i = 0; i<message_count; i++)
        src->wlog(m::tracing::event_kind::error, L"Hello, tracing this should definitely show up!");
}

TEST(TestFastSink, LotsOfMessages50)
{
    auto fastsink = fast_sink::register_sink(m::tracing::monitor.get());
    auto src      = m::tracing::monitor->make_source();

    constexpr auto message_count = 50;

    for (auto i = 0; i < message_count; i++)
        src->wlog(m::tracing::event_kind::error, L"Hello, tracing this should definitely show up!");
}

TEST(TestFastSink, LotsOfMessages100)
{
    auto fastsink = fast_sink::register_sink(m::tracing::monitor.get());
    auto src      = m::tracing::monitor->make_source();

    constexpr auto message_count = 100;

    for (auto i = 0; i < message_count; i++)
        src->wlog(m::tracing::event_kind::error, L"Hello, tracing this should definitely show up!");
}

TEST(TestFastSink, LotsOfMessages500)
{
    auto fastsink = fast_sink::register_sink(m::tracing::monitor.get());
    auto src      = m::tracing::monitor->make_source();

    constexpr auto message_count = 500;

    for (auto i = 0; i < message_count; i++)
        src->wlog(m::tracing::event_kind::error, L"Hello, tracing this should definitely show up!");
}

TEST(TestFastSink, LotsOfMessages1000)
{
    auto fastsink = fast_sink::register_sink(m::tracing::monitor.get());
    auto src      = m::tracing::monitor->make_source();

    constexpr auto message_count = 1000;

    for (auto i = 0; i < message_count; i++)
        src->wlog(m::tracing::event_kind::error, L"Hello, tracing this should definitely show up!");
}

TEST(TestFastSink, LotsOfMessages10000)
{
    auto fastsink = fast_sink::register_sink(m::tracing::monitor.get());
    auto src      = m::tracing::monitor->make_source();

    constexpr auto message_count = 10000;

    for (auto i = 0; i < message_count; i++)
        src->wlog(m::tracing::event_kind::error, L"Hello, tracing this should definitely show up!");
}

TEST(TestFastSink, LotsOfMessages100000)
{
    auto fastsink = fast_sink::register_sink(m::tracing::monitor.get());
    auto src      = m::tracing::monitor->make_source();

    constexpr auto message_count = 100000;

    for (auto i = 0; i < message_count; i++)
        src->wlog(m::tracing::event_kind::error, L"Hello, tracing this should definitely show up!");
}







