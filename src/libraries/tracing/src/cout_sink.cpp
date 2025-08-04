// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <iostream>
#include <memory>
#include <print>
#include <string_view>

#include <m/strings/literal_string_view.h>
#include <m/tracing/cout_sink.h>
#include <m/tracing/tracing.h>

using namespace m::string_view_literals;

namespace m::tracing
{
    cout_sink::cout_sink(m::not_null<monitor_class*> monitor):
        sink(L"cout_sink"_sl, monitor), m_done{false}, m_thread([this]() { this->sink_thread(); })
    {}

    // For all console output, just enqueue the input to the message queue, tracking
    // the return queue for the message. The sink thread will output and return the
    // message to the original correct queue.
    on_message_disposition
    cout_sink::on_message(may_forward_message_option may_forward_message, envelope& env)
    {
        auto l = std::unique_lock(m_mutex);

        if (m_closed)
            return on_message_disposition::message_processed;

        if (may_forward_message == may_forward_message_option::may_forward_message)
        {
            m_message_queue.enqueue(env);
            return on_message_disposition::message_forwarded;
        }
        else
        {
            auto msg_copy = m_monitor->duplicate_message(env);
            m_message_queue.enqueue(msg_copy);
            return on_message_disposition::message_processed;
        }
    }

    void
    cout_sink::process_message(message* msg)
    {
        std::array<wchar_t, 16384> buffer;

        if (msg != nullptr)
        {
            auto it    = safe_array_iterator(buffer, 0);
            auto itend = std::format_to(it,
                                        L"[k{} p({}) t({}) @ {}Z] {}\n",
                                        msg->kind(),
                                        msg->m_event_context.os_process_id(),
                                        msg->m_event_context.os_thread_id(),
                                        msg->m_event_context.time_point(),
                                        msg->view());

            std::wcout << std::wstring_view(&*it, &*itend);
        }
    }

    // For the console output, just sit in a loop, dequeueing messages. As long
    // as they come back not null, print them and return the buffers to their
    // owners.
    void
    cout_sink::sink_thread()
    {
        while (!m_stop.load(std::memory_order_acquire))
        {
            while (!m_stop.load(std::memory_order_acquire) && !m_message_queue.empty())
            {
                auto env = m_message_queue.dequeue();
                process_message(env.message());
            }

            // m_done should be set before setting m_stop but no harm checking.
            if (m_done.load(std::memory_order_acquire) || m_stop.load(std::memory_order_acquire))
                break;

            m_message_queue.wait();
        }
    }

    bool
    cout_sink::could_forward_message(envelope const&)
    {
        return true;
    }

    std::unique_ptr<sink_registration>
    cout_sink::register_sink(std::wstring_view channel_name, m::not_null<monitor_class*> monitor)
    {
        std::shared_ptr<cout_sink> expected = ms_cout_sink.load(std::memory_order_acquire);

        if (!expected)
        {
            for (;;)
            {
                auto desired = std::make_shared<cout_sink>(monitor);

                if (ms_cout_sink.compare_exchange_strong(
                        expected, desired, std::memory_order_acq_rel))
                    break;
            }

            expected = ms_cout_sink.load(std::memory_order_acquire);
        }

        return monitor->register_sink(channel_name, expected);
    }

    void
    cout_sink::close(close_flush_option cfo) noexcept
    {
        std::thread t;

        {
            auto l = std::unique_lock(m_mutex);
            if (!m_done.load(std::memory_order_acquire))
            {
                m_done.store(true, std::memory_order_release);
                m_closed = true;

                switch (cfo)
                {
                    case close_flush_option::abandon:
                    {
                        m_stop.store(true, std::memory_order_release);
                        m_message_queue.wake_waiters();

                        auto queue_size = m_message_queue.size();
                        if (queue_size != 0)
                            std::wcout << L"[Abandoning " << queue_size
                                       << L" items in the stdout queue]\n";

                        break;
                    }

                    case close_flush_option::expedite:
                    {
                        // Tell the thread to stop processing and terminate if
                        // possible.

                        m_stop.store(true, std::memory_order_release);
                        m_message_queue.wake_waiters();

                        // But we will complete the queue, not subject
                        // to the vagaries of how the other thread may be
                        // scheduled.
                        while (!m_message_queue.empty())
                        {
                            auto env = m_message_queue.dequeue();
                            process_message(env.message());
                        }

                        break;
                    }

                    case close_flush_option::normal:
                    {
                        // wake the thread and let it shut down. Nothing more drastic to do.
                        m_message_queue.wake_waiters();
                        break;
                    }
                }

                using std::swap;
                swap(t, m_thread);
            }
        }

        if (t.joinable())
            t.join();

        std::cout.flush();
    }

} // namespace m::tracing