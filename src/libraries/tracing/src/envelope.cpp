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
        m_message_source{other.m_message_source}, m_imessage{}
    {
        using std::swap;

        swap(m_imessage, other.m_imessage);
    }

    void
    envelope::operator=(envelope&& other) noexcept
    {
        using std::swap;

        swap(m_imessage, other.m_imessage);
        swap(m_message_source, other.m_message_source);
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
