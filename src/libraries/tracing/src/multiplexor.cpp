// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>

#include <m/tracing/monitor_class.h>
#include <m/tracing/multiplexor.h>

namespace m::tracing
{
    multiplexor::multiplexor(m::not_null<monitor_class*>              monitor,
                             topology_version                         topver,
                             std::initializer_list<std::wstring_view> channel_names):
        m_monitor{monitor},
        m_channel_names(channel_names.begin(), channel_names.end()),
        m_topology_version{topver}
    {
        for (auto&& e: m_channel_names)
            m_monitor->for_each_channel_sink(
                m::locked, e, [this](auto snk) { m_sink_shims.emplace_back(snk); });
    }

    on_message_disposition
    multiplexor::on_message(envelope& env)
    {
        auto l = std::unique_lock(m_mutex);

        if (m_sink_shims.size() == 0)
        {
            return on_message_disposition::completed;
        }

        if (m_sink_shims.size() == 1)
        {
            return m_sink_shims[0]->on_message(may_queue_option::may_queue, env);
        }

        for (auto&& snk: m_sink_shims)
        {
            // It would be a good optimization to avoid making a copy
            // if there was a single sink.
            if (snk->would_queue(env))
            {
                auto msg_copy = m_monitor->copy_message(m::locked, env);
                std::ignore   = snk->on_message(may_queue_option::may_queue, msg_copy);
            }
            else
            {
                std::ignore = snk->on_message(may_queue_option::may_not_queue, env);
            }
        }

        return on_message_disposition::completed;
    }

    envelope
    multiplexor::allocate_message(event_kind kind)
    {
        return m_monitor->allocate_message(kind);
    }

} // namespace m::tracing