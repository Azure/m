// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>

#include <m/tracing/monitor_class.h>
#include <m/tracing/multiplexor.h>
#include <m/tracing/sink_shim.h>

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

    void
    multiplexor::close(close_flush_option cfo)
    {
        auto l = std::unique_lock(m_mutex);

        for (auto&& s: m_sink_shims)
            s->close(cfo);
    }

    on_message_disposition
    multiplexor::on_message(may_queue_option, envelope& env)
    {
        auto l = std::unique_lock(m_mutex);

        if (m_topology_version != m_monitor->get_topology_version())
            rebuild_topology_from_monitor();

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
                auto       msg_copy    = m_monitor->copy_message(m::locked, env);
                auto const disposition = snk->on_message(may_queue_option::may_queue, msg_copy);
                if (disposition == on_message_disposition::completed)
                    return_to_sender(msg_copy);
            }
            else
            {
                std::ignore = snk->on_message(may_queue_option::may_not_queue, env);
            }
        }

        return on_message_disposition::completed;
    }

    [[nodiscard]] bool
    multiplexor::would_queue(envelope const& env)
    {
        auto l = std::unique_lock(m_mutex);

        for (auto&& s: m_sink_shims)
            if (s->would_queue(env))
                return true;

        return false;
    }

    void
    multiplexor::rebuild_topology_from_monitor() noexcept
    {
        // The usual version trick: since we can't lock the monitor, we get the
        // version sequence number before querying its state,
        // so if the version number changes after this point, we may
        // refresh needlessly but we won't lose an update.
        m_topology_version = m_monitor->get_topology_version();

        std::vector<std::shared_ptr<m::tracing::internal::sink_shim>> new_sink_shims;

        for (auto&& e: m_channel_names)
            m_monitor->for_each_channel_sink(
                e,
                [&](auto snk) { new_sink_shims.emplace_back(snk); });

        using std::swap;
        swap(m_sink_shims, new_sink_shims);
    }

    envelope
    multiplexor::allocate_message(event_kind kind)
    {
        return m_monitor->allocate_message(kind);
    }

    void
    multiplexor::deallocate_message(m::not_null<tracing::message*> msg) noexcept
    {
        m_monitor->deallocate_message(msg);
    }

} // namespace m::tracing