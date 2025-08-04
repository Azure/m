// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "sink_shim.h"

namespace m::tracing_impl
{
    sink_shim::sink_shim() {}
    sink_shim::sink_shim(std::shared_ptr<m::tracing::sink> const& s): m_sink(s) {}

    sink_shim::sink_shim(sink_shim&& other) noexcept
    {
        using std::swap;

        swap(m_sink, other.m_sink);
    }

    sink_shim&
    sink_shim::operator=(sink_shim&& other) noexcept
    {
        using std::swap;

        swap(m_sink, other.m_sink);

        return *this;
    }

    bool
    sink_shim::could_forward_message(m::tracing::envelope const& item)
    {
        auto l = std::unique_lock(m_mutex);

        if (!m_sink)
            return false;

        return m_sink->could_forward_message(item);
    }

    m::tracing::on_message_disposition
    sink_shim::on_message(m::tracing::may_forward_message_option may_forward_message,
                          m::tracing::envelope&                  item)
    {
        auto l = std::unique_lock(m_mutex);

        if (!m_sink)
            return m::tracing::on_message_disposition::message_processed;

        return m_sink->on_message(may_forward_message, item);
    }

    void
    sink_shim::close(m::tracing::close_flush_option cfo) noexcept
    {
        auto l = std::unique_lock(m_mutex);

        if (!m_sink)
            return;

        m_sink->close(cfo);
        m_sink.reset();
    }

    bool
    sink_shim::closed() noexcept
    {
        return m_sink.get() == nullptr;
    }

} // namespace m::tracing_impl
