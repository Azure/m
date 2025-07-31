// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <condition_variable>
#include <mutex>

#include <m/tracing/message_source.h>

namespace m::tracing
{
    void
    return_to_sender(tracing::envelope const& env)
    {
        //
        auto msg = env.message();
        if (msg != nullptr)
        {
            env.message_source()->deallocate_message(msg);
        }
    }

    message_allocator::message_allocator(m::not_null<message_source*> source, event_kind kind):
        m_envelope(source->allocate_message(kind)), m_armed(true)
    {}

    message_allocator::~message_allocator()
    {
        if (m_armed)
        {
            return_to_sender(m_envelope);
        }
    }

    void
    message_allocator::release()
    {
        m_envelope.message(nullptr);
    }

    envelope&
    message_allocator::env()
    {
        return m_envelope;
    }

    envelope&&
    message_allocator::move_env()
    {
        m_armed = false;
        return std::move(m_envelope);
    }

} // namespace m::tracing
