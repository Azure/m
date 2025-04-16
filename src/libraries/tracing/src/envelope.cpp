// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <condition_variable>
#include <mutex>

#include <m/tracing/envelope.h>
#include <m/tracing/message.h>
#include <m/tracing/message_queue.h>

namespace m::tracing
{
    envelope::envelope(gsl::not_null<message*> msg):
        m_message(msg), m_return_queue(nullptr)
    {}

    envelope::envelope(gsl::not_null<message*>       msg,
                                 gsl::not_null<message_queue*> return_queue):
        m_message(msg), m_return_queue(return_queue)
    {}

    envelope::envelope(envelope&& other) noexcept: m_message{}, m_return_queue{}
    {
        using std::swap;

        swap(m_message, other.m_message);
        swap(m_return_queue, other.m_return_queue);
    }

    void
    envelope::operator=(envelope&& other) noexcept
    {
        using std::swap;

        swap(m_message, other.m_message);
        swap(m_return_queue, other.m_return_queue);
    }

    message*
    envelope::get_message() const
    {
        return m_message;
    }

    message_queue*
    envelope::get_message_queue() const
    {
        return m_return_queue;
    }

    void
    envelope::reset()
    {
        if (m_return_queue != nullptr)
        {
            std::exchange(m_return_queue, nullptr)
                ->enqueue(gsl::not_null<message*>(std::exchange(m_message, nullptr)));
        }
    }

    void
    envelope::reset(envelope const& other, gsl::not_null<message_queue*> return_queue)
    {
        if (m_return_queue != nullptr)
        {
            std::exchange(m_return_queue, nullptr)
                ->enqueue(gsl::not_null<message*>(std::exchange(m_message, nullptr)));
        }

        *m_message     = *other.m_message;
        m_return_queue = return_queue;
    }

    envelope::~envelope()
    {
        if (m_return_queue != nullptr)
            m_return_queue->enqueue(gsl::not_null<message*>(m_message));
    }
} // namespace m::tracing
