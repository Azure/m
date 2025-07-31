// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <condition_variable>
#include <mutex>

#include <m/tracing/envelope.h>
#include <m/tracing/message.h>
#include <m/tracing/message_queue.h>

namespace m::tracing
{
    envelope::envelope(m::not_null<tracing::message_source*> source, tracing::message* msg):
        m_message_source(source), m_message(msg)
    {}

    envelope::envelope(envelope&& other) noexcept:
        m_message_source{other.m_message_source}, m_message{}
    {
        using std::swap;

        swap(m_message, other.m_message);
    }

    void
    envelope::operator=(envelope&& other) noexcept
    {
        using std::swap;

        swap(m_message, other.m_message);
        swap(m_message_source, other.m_message_source);
    }

    void
    envelope::swap(envelope& other) noexcept
    {
        using std::swap;

        swap(m_message, other.m_message);
        swap(m_message_source, other.m_message_source);
    }

    tracing::message*
    envelope::message() const
    {
        return m_message;
    }

    tracing::message*
    envelope::message(tracing::message* msg)
    {
        return std::exchange(m_message, msg);
    }

    m::not_null<tracing::message_source*>
    envelope::message_source() const
    {
        return m_message_source;
    }

    envelope::~envelope() { m_message = nullptr; }
} // namespace m::tracing
