// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/tracing/sink_shim.h>

namespace m::tracing::internal
{
    sink_shim::sink_shim(): m_closed(true) {}
    sink_shim::sink_shim(std::shared_ptr<sink> const& s): m_sink(s), m_closed{!m_sink} {}

    sink_shim::sink_shim(sink_shim&& other) noexcept: m_closed(true)
    {
        using std::swap;

        swap(m_sink, other.m_sink);
        swap(m_closed, other.m_closed);
    }

    sink_shim&
    sink_shim::operator=(sink_shim&& other) noexcept
    {
        using std::swap;

        swap(m_sink, other.m_sink);
        swap(m_closed, other.m_closed);

        return *this;
    }

    bool
    sink_shim::would_queue(envelope const& item)
    {
        auto l = std::unique_lock(m_mutex);

        if (m_closed || !m_sink)
            return false;

        return m_sink->would_queue(item);
    }

    on_message_disposition
    sink_shim::on_message(may_queue_option may_queue, envelope& item)
    {
        auto l = std::unique_lock(m_mutex);

        if (m_closed || !m_sink)
            return on_message_disposition::completed;

        return m_sink->on_message(may_queue, item);
    }

    void
    sink_shim::close() noexcept
    {
        auto l = std::unique_lock(m_mutex);

        if (m_closed || !m_sink)
            return;

        m_sink->close();
        m_sink.reset();

        m_closed = true;
    }

} // namespace m::tracing::internal
