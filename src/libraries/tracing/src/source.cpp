// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <string_view>
#include <utility>

#include <gsl/gsl>

#include <m/tracing/tracing.h>

namespace m::tracing
{
    source::source(gsl::not_null<monitor_class*>            monitor,
                   event_kind                               kind,
                   std::initializer_list<std::wstring_view> channel_names):
        m_monitor(monitor),
        m_multiplexor(m_monitor->get_multiplexor(channel_names)),
        m_event_kind(kind),
        m_channel_names(channel_names.begin(), channel_names.end())
    {}

    source::source(gsl::not_null<monitor_class*> monitor,
                   event_kind                    kind,
                   std::wstring_view             channel_name):
        m_monitor(monitor),
        m_multiplexor(m_monitor->get_multiplexor({channel_name})),
        m_event_kind(kind)
    {
        m_channel_names.emplace_back(channel_name);
    }

    void
    source::close()
    {
        if (!m_closed)
        {
            m_closed = true;
            do_close();
        }
    }

    void
    source::do_close()
    {
        //
    }

    bool
    source::is_closed() const
    {
        return m_closed;
    }

    bool
    source::do_test_kind(event_kind kind)
    {
        return std::to_underlying(m_event_kind) >= std::to_underlying(kind);
    }
} // namespace m::tracing