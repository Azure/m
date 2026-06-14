// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <condition_variable>
#include <mutex>

#include <m/tracing/envelope.h>
#include <m/tracing/message.h>
#include <m/tracing/message_queue.h>

namespace m::tracing
{
    envelope::envelope(m::not_null<tracing::message_source*> source, tracing::imessage* msg):
        m_message_source(source), m_imessage(msg)
    {}

    envelope::envelope(envelope&& other) noexcept:
        m_message_source{std::exchange(other.m_message_source, nullptr)},
        m_imessage{std::exchange(other.m_imessage, nullptr)}
    {}

    void
    envelope::operator=(envelope&& other) noexcept
    {
        if (this == &other)
            return;

        // Release any message we currently hold back to its source before taking
        // ownership of other's message. A plain swap would move our live message
        // into other, whose destructor only nulls the pointer (it does not return
        // the slot), permanently leaking that message buffer from the pool.
        return_to_sender();

        // Take ownership of other's message and source, leaving the moved-from
        // envelope in a consistent empty state (both pointers null) so it does
        // not retain a dangling source.
        m_message_source = std::exchange(other.m_message_source, nullptr);
        m_imessage       = std::exchange(other.m_imessage, nullptr);
    }

    void
    envelope::swap(envelope& other) noexcept
    {
        using std::swap;

        swap(m_imessage, other.m_imessage);
        swap(m_message_source, other.m_message_source);
    }

    tracing::imessage*
    envelope::message() const
    {
        return m_imessage;
    }

    // tracing::imessage*
    // envelope::message(tracing::imessage* msg)
    //{
    //     return std::exchange(m_imessage, msg);
    // }

    void
    envelope::return_to_sender()
    {
        if (m_imessage == nullptr)
            return;

        M_INTERNAL_ERROR_CHECK(m_message_source != nullptr);

        m_message_source->deallocate_message(m_imessage);

        m_imessage       = nullptr;
        m_message_source = nullptr;
    }

    tracing::message_source*
    envelope::message_source() const
    {
        return m_message_source;
    }

    envelope::~envelope() { m_imessage = nullptr; }
} // namespace m::tracing
