// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <iostream>
#include <memory>
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
    cout_sink::on_message(may_queue_option may_queue, envelope& env)
    {
        auto l = std::unique_lock(m_mutex);

        if (m_closed)
            return on_message_disposition::completed;

        if (may_queue == may_queue_option::may_queue)
        {
            m_message_queue.enqueue(env);
            return on_message_disposition::queued;
        }
        else
        {
            auto msg_copy = m_monitor->copy_message(env);
            m_message_queue.enqueue(msg_copy);
            return on_message_disposition::completed;
        }
    }

    // For the console output, just sit in a loop, dequeueing messages. As long
    // as they come back not null, print them and return the buffers to their
    // owners.
    void
    cout_sink::sink_thread()
    {
        std::array<wchar_t, 16384> buffer;

        for (;;)
        {
            while (!m_message_queue.empty())
            {
                auto env = m_message_queue.dequeue();
                auto msg = env.get_message();

                if (msg != nullptr)
                {
                    auto it = safe_array_iterator(buffer, 0);
                    auto itend = std::format_to(it,
                                                L"[p({}) t({}) @ {}Z] {}\n",
                                                msg->m_event_context.m_process_id,
                                                msg->m_event_context.m_thread_id,
                                                msg->m_event_context.m_time_point,
                                                msg->view());

                    std::wcout << std::wstring_view(&*it, &*itend);
            }
        }

        if (m_done)
            break;

        m_message_queue.wait();
    }
}

bool
cout_sink::would_queue(envelope const&)
{
    return true;
}

void
cout_sink::register_sink(m::not_null<monitor_class*> monitor)
{
    std::shared_ptr<cout_sink> expected = ms_cout_sink.load(std::memory_order_acquire);

    if (!expected)
    {
        for (;;)
        {
            auto desired = std::make_shared<cout_sink>(monitor);

            if (ms_cout_sink.compare_exchange_strong(expected, desired, std::memory_order_acq_rel))
                break;
        }

        expected = ms_cout_sink.load(std::memory_order_acquire);
    }

    monitor->register_sink(expected);
}

void
cout_sink::close()
{
    {
        auto l = std::unique_lock(m_mutex);
        if (!m_done)
        {
            m_done   = true;
            m_closed = true;
            m_message_queue.wake_waiters();
        }
    }
    m_thread.join();
}

} // namespace m::tracing