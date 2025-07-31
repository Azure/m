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
    class slow_sink : public m::tracing::sink
    {
    public:
        template <typename Rep, typename Period>
        slow_sink(m::not_null<m::tracing::monitor_class*> monitor,
                  std::chrono::duration<Rep, Period>      delay):
            sink(L"cout_sink"_sl, monitor),
            m_delay(std::chrono::duration_cast<std::chrono::milliseconds>(delay)),
            m_done{false}
        {}

        virtual ~slow_sink() {}

        // Kind of hokey but who is responsible for registering the
        // cout based sink? This is how it's done I guess
        template <typename Rep, typename Period>
        static std::unique_ptr<m::tracing::sink_registration>
        register_sink(m::not_null<m::tracing::monitor_class*> monitor,
                      std::chrono::duration<Rep, Period>      delay)
        {
            std::shared_ptr<slow_sink> expected = ms_slow_sink.load(std::memory_order_acquire);

            if (!expected)
            {
                for (;;)
                {
                    auto desired = std::make_shared<slow_sink>(monitor, delay);

                    if (ms_slow_sink.compare_exchange_strong(
                            expected, desired, std::memory_order_acq_rel))
                        break;
                }

                expected = ms_slow_sink.load(std::memory_order_acquire);
            }

            return monitor->register_sink(expected);
        }

    protected:
        m::tracing::on_message_disposition
        on_message(m::tracing::may_queue_option, m::tracing::envelope&) override
        {
            auto l = std::unique_lock(m_mutex);

            if (m_closed)
                return m::tracing::on_message_disposition::completed;

            // Literally, all we do is wait for the delay.
            std::this_thread::sleep_for(m_delay);

            return m::tracing::on_message_disposition::completed;
        }

        bool
        would_queue(m::tracing::envelope const&) override
        {
            return true;
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
        std::chrono::milliseconds                             m_delay;
        bool                                                  m_done;
        static inline std::atomic<std::shared_ptr<slow_sink>> ms_slow_sink;
    };

    constexpr auto slow_sink_delay_1 = 10ms;
} // namespace

TEST(TestSlowSink, RegisterSink)
{
    auto coutsink = slow_sink::register_sink(&m::tracing::monitor, slow_sink_delay_1);
}

TEST(TestSlowSink, LogAnEventNoFormattingWithConsoleSink)
{
    auto coutsink = slow_sink::register_sink(&m::tracing::monitor, slow_sink_delay_1);

    auto src = m::tracing::monitor.make_source();

    src->log(m::tracing::event_kind::information, "Hello, tracing!");
}

TEST(TestSlowSink, LogATracingEventNoFormattingWithConsoleSink)
{
    auto coutsink = slow_sink::register_sink(&m::tracing::monitor, slow_sink_delay_1);

    auto src = m::tracing::monitor.make_source();

    src->log(m::tracing::event_kind::tracing, "Hello, tracing this should not show up!");
}

TEST(TestSlowSink, LogAErrorEventNoFormattingWithConsoleSink)
{
    auto coutsink = slow_sink::register_sink(&m::tracing::monitor, slow_sink_delay_1);

    auto src = m::tracing::monitor.make_source();

    src->log(m::tracing::event_kind::error, "Hello, tracing this should definitely show up!");
}

TEST(TestSlowSink, WLogAnEventNoFormattingWithConsoleSink)
{
    auto coutsink = slow_sink::register_sink(&m::tracing::monitor, slow_sink_delay_1);

    auto src = m::tracing::monitor.make_source();

    src->wlog(m::tracing::event_kind::information, L"Hello, tracing!");
}

TEST(TestSlowSink, WLogATracingEventNoFormattingWithConsoleSink)
{
    auto coutsink = slow_sink::register_sink(&m::tracing::monitor, slow_sink_delay_1);

    auto src = m::tracing::monitor.make_source();

    src->wlog(m::tracing::event_kind::tracing, L"Hello, tracing this should not show up!");
}

TEST(TestSlowSink, WLogAErrorEventNoFormattingWithConsoleSink)
{
    auto coutsink = slow_sink::register_sink(&m::tracing::monitor, slow_sink_delay_1);

    auto src = m::tracing::monitor.make_source();

    src->wlog(m::tracing::event_kind::error, L"Hello, tracing this should definitely show up!");
}

TEST(TestSlowSink, LogMessagesAfterClosingSink)
{
    auto coutsink = slow_sink::register_sink(&m::tracing::monitor, slow_sink_delay_1);
    auto src      = m::tracing::monitor.make_source();

    src->wlog(m::tracing::event_kind::error, L"Hello, tracing this should definitely show up!");
    coutsink.reset();
    src->wlog(m::tracing::event_kind::error,
              L"This is another event but after the sink was closed");
}

TEST(TestSlowSink, LotsOfMessages10)
{
    auto coutsink = slow_sink::register_sink(&m::tracing::monitor, slow_sink_delay_1);
    auto src      = m::tracing::monitor.make_source();

    constexpr auto message_count = 10;

    for (auto i = 0; i<message_count; i++)
        src->wlog(m::tracing::event_kind::error, L"Hello, tracing this should definitely show up!");
}

TEST(TestSlowSink, LotsOfMessages50)
{
    auto coutsink = slow_sink::register_sink(&m::tracing::monitor, slow_sink_delay_1);
    auto src      = m::tracing::monitor.make_source();

    constexpr auto message_count = 50;

    for (auto i = 0; i < message_count; i++)
        src->wlog(m::tracing::event_kind::error, L"Hello, tracing this should definitely show up!");
}

TEST(TestSlowSink, LotsOfMessages100)
{
    auto coutsink = slow_sink::register_sink(&m::tracing::monitor, slow_sink_delay_1);
    auto src      = m::tracing::monitor.make_source();

    constexpr auto message_count = 100;

    for (auto i = 0; i < message_count; i++)
        src->wlog(m::tracing::event_kind::error, L"Hello, tracing this should definitely show up!");
}

TEST(TestSlowSink, LotsOfMessages500)
{
    auto coutsink = slow_sink::register_sink(&m::tracing::monitor, slow_sink_delay_1);
    auto src      = m::tracing::monitor.make_source();

    constexpr auto message_count = 500;

    for (auto i = 0; i < message_count; i++)
        src->wlog(m::tracing::event_kind::error, L"Hello, tracing this should definitely show up!");
}

TEST(TestSlowSink, LotsOfMessages1000)
{
    auto coutsink = slow_sink::register_sink(&m::tracing::monitor, slow_sink_delay_1);
    auto src      = m::tracing::monitor.make_source();

    constexpr auto message_count = 1000;

    for (auto i = 0; i < message_count; i++)
        src->wlog(m::tracing::event_kind::error, L"Hello, tracing this should definitely show up!");
}







