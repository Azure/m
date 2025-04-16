// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <condition_variable>
#include <mutex>

#include <m/tracing/message.h>

namespace m::tracing
{
    void
    message::operator=(message const& other)
    {
        std::copy_n(other.m_chars.begin(), other.m_length, m_chars.begin());
        m_length        = other.m_length;
        m_event_context = other.m_event_context;
    }

    std::wstring_view
    message::view()
    {
        return std::wstring_view(m_chars.data(), m_length);
    }

} // namespace m::tracing
