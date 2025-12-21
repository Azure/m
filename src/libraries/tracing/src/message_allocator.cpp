// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <condition_variable>
#include <mutex>

#include <m/tracing/message_allocator.h>
#include <m/tracing/message_processor.h>
#include <m/tracing/message_source.h>

#include "tracing_internal.h"

namespace
{
    using m::tracing_impl::tr_frame;
}

namespace m::tracing
{
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
        tr_frame frame(__FUNCTION__, this);
        m_envelope.return_to_sender();
        frame.succeeded();
    }

    envelope&
    message_allocator::env()
    {
        return m_envelope;
    }

    m::not_null<tracing::imessage*>
    message_allocator::message()
    {
        return m_envelope.message();
    }

    envelope&&
    message_allocator::move_env()
    {
        m_armed = false;
        return std::move(m_envelope);
    }

    void
    message_allocator::send_message(m::not_null<m::tracing::message_processor*> processor)
    {
        auto result = processor->on_message(may_forward_message_option::may_forward_message, m_envelope);
        if (result == on_message_disposition::message_forwarded)
        {
            // If the message was enqueued on, we do not want to try to deallocate
            release();
        }
    }

} // namespace m::tracing
