// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <format>
#include <initializer_list>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <m/error_handling/macros.h>
#include <m/strings/literal_string_view.h>
#include <m/tracing/close_flush_option.h>
#include <m/tracing/envelope.h>
#include <m/tracing/format_view.h>
#include <m/tracing/message_queue.h>
#include <m/tracing/sink.h>
#include <m/tracing/tracing.h>

using namespace m::string_view_literals;

namespace m
{
    namespace tracing
    {
        template <typename CharT>
        class basic_ostream_sink : public sink
        {
        public:
            basic_ostream_sink(m::not_null<monitor_class*> monitor):
                sink(L"cout_sink"_sl, monitor),
                m_done{false},
                m_thread([this]() { this->sink_thread(); })
            {}

            virtual ~basic_ostream_sink() = default;

            // Kind of hokey but who is responsible for registering the
            // cout based sink? This is how it's done I guess
            static std::unique_ptr<sink_registration>
            register_sink(std::wstring_view channel_name, m::not_null<monitor_class*> monitor)
            {
                std::shared_ptr<basic_ostream_sink> expected =
                    ms_sink.load(std::memory_order_acquire);

                if (!expected)
                {
                    for (;;)
                    {
                        auto desired = std::make_shared<basic_ostream_sink>(monitor);

                        if (ms_sink.compare_exchange_strong(
                                expected, desired, std::memory_order_acq_rel))
                            break;
                    }

                    expected = ms_sink.load(std::memory_order_acquire);
                }

                return monitor->register_sink(channel_name, expected);
            }

        protected:
            on_message_disposition
            on_message(may_forward_message_option may_forward_message, envelope& env) override
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

            bool
            could_forward_message(envelope const&) override
            {
                return true;
            }

            void
            close(close_flush_option cfo) noexcept override
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

        private:
            message_queue     m_message_queue;
            std::atomic<bool> m_done;
            std::atomic<bool> m_stop;
            std::thread       m_thread;

            static inline std::atomic<std::shared_ptr<basic_ostream_sink>> ms_sink;

            void
#pragma warning(suppress : 6262)
            process_message(imessage* msg)
            {
                std::array<CharT, 16384> buffer;

                if (msg != nullptr)
                {
                    auto it = safe_array_iterator(buffer, 0);

                    if constexpr (std::is_same_v<CharT, char>)
                    {
                        auto itend = std::format_to(it,
                                                    "[{:b} {:04x}.{:04x} @ {}Z] {}\n",
                                                    msg->kind(),
                                                    msg->event_context()->os_process_id(),
                                                    msg->event_context()->os_thread_id(),
                                                    msg->event_context()->time_point(),
                                                    format_view(msg->view()));
                        std::cout << std::basic_string_view<CharT>(&*it, &*itend);
                    }
                    else if constexpr (std::is_same_v<CharT, wchar_t>)
                    {
                        auto itend = std::format_to(it,
                                                    L"[{:b} {:04x}.{:04x} @ {}Z] {}\n",
                                                    msg->kind(),
                                                    msg->event_context()->os_process_id(),
                                                    msg->event_context()->os_thread_id(),
                                                    msg->event_context()->time_point(),
                                                    format_view(msg->view()));
                        std::wcout << std::basic_string_view<CharT>(&*it, &*itend);
                    }
                    else
                    {
                        M_NOT_IMPLEMENTED("only char and wchar_t are implemented");
                    }
                }
            }

            // For the console output, just sit in a loop, dequeueing messages. As long
            // as they come back not null, print them and return the buffers to their
            // owners.
            void
            sink_thread()
            {
                while (!m_stop.load(std::memory_order_acquire))
                {
                    // Sample the wake generation before draining and before
                    // checking the termination flags. Passing this baseline to
                    // wait() means any wake_waiters() that races in after this
                    // point advances the generation and makes wait() return
                    // immediately rather than block, closing the teardown race.
                    auto const wake_gen = m_message_queue.wake_generation();

                    while (!m_stop.load(std::memory_order_acquire) && !m_message_queue.empty())
                    {
                        auto env = m_message_queue.dequeue();
                        process_message(env.message());
                    }

                    // m_done should be set before setting m_stop but no harm checking.
                    if (m_done.load(std::memory_order_acquire) ||
                        m_stop.load(std::memory_order_acquire))
                        break;

                    m_message_queue.wait(wake_gen);
                }
            }
        };

        using cout_sink  = basic_ostream_sink<char>;
        using wcout_sink = basic_ostream_sink<wchar_t>;
    } // namespace tracing
} // namespace m
