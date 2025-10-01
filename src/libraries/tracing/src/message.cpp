// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <condition_variable>
#include <mutex>

#include <m/tracing/message.h>

namespace m::tracing
{
    void
    message::copy_into(m::not_null<imessage*> msg)
    {
        if (auto othermsg = dynamic_cast<message*>(static_cast<imessage*>(msg)); othermsg != nullptr)
        {
            othermsg->m_event_kind = m_event_kind;
            othermsg->m_buffer.assign(m_buffer);
            othermsg->m_event_context = m_event_context;
        }
        else
        {
            M_NOT_IMPLEMENTED("Heterogeneous message types not implemented, how did this happen?");
        }
    }

    std::wstring_view
    message::view()
    {
        return std::wstring_view(m_buffer.c_str());
    }

    event_kind
    message::kind() const
    {
        return m_event_kind;
    }

    void
    message::kind(event_kind kind)
    {
        m_event_kind = kind;
    }

    void
    message::clear()
    {
        m_buffer.clear();
    }

    void
    message::push_back(wchar_t const& wch)
    {
        m_buffer.push_back(wch);
    }

    void
    message::event_context(tracing::event_context const& ec)
    {
        m_event_context = ec;
    }

    tracing::event_context const*
    message::event_context() const
    {
        return &m_event_context;
    }

} // namespace m::tracing
