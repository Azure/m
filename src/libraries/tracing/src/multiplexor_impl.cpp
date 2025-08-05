// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <algorithm>
#include <memory>

#include <m/error_handling/macros.h>
#include <m/tracing/monitor_class.h>
#include <m/tracing/multiplexor.h>
#include <m/utility/compiler.h>

#include "monitor_class_impl.h"
#include "multiplexor_impl.h"
#include "sink_shim.h"

namespace m::tracing_impl
{
    multiplexor::multiplexor(m::not_null<monitor*>                    monitor,
                             m::tracing::topology_version             topver,
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
    multiplexor::close(m::tracing::close_flush_option cfo) noexcept
    {
        auto l = std::unique_lock(m_mutex);

        for (auto&& s: m_sink_shims)
            s->close(cfo);
    }

    m::tracing::on_message_disposition
    multiplexor::on_message(m::tracing::may_forward_message_option mfmo, m::tracing::envelope& env)
    {
        std::array<std::shared_ptr<sink_shim>, 4> sinks;

        auto l = std::unique_lock(m_mutex);

        if (m_topology_version != m_monitor->get_topology_version())
            rebuild_topology_from_monitor();

        auto const sink_count = m_sink_shims.size();

        // Handle the "no sinks" case here. We special case the last shim
        // later on so this lets us work with "sink_count - 1" without worrying
        // about underflowing.
        if (sink_count == 0)
            return m::tracing::on_message_disposition::message_processed;

        // Lots of sinks? handle that in another call frame
        if (sink_count > sinks.size())
            return handle_large_dispatch(l, mfmo, env);

        std::copy(m_sink_shims.begin(), m_sink_shims.end(), sinks.begin());

        l.unlock();

        //
        // If there is more than one sink, see if any of them could
        // forward the messages, and if so, move them to the last
        // position.
        //

        if (sink_count > 1)
        {
            std::size_t i = sink_count;

            while (i > 0)
            {
                i--;

                if (sinks[i]->could_forward_message(env))
                {
                    // If this isn't still the last sink, just swap the one that could
                    // forward into the last position. Either way, call it done.

                    if (i != sink_count - 1)
                    {
                        using std::swap;
                        swap(sinks[i], sinks[sink_count - 1]);
                    }

                    break;
                }
            }
        }

        //
        // Go through all the sinks except the last one to process the messages. They
        // are not allowed to forward the message.
        //
        for (std::size_t i = 0; i < sink_count - 1; i++)
        {
            auto const disp = sinks[i]->on_message(
                m::tracing::may_forward_message_option::may_not_forward_message, env);
            M_INTERNAL_ERROR_CHECK(disp == m::tracing::on_message_disposition::message_processed);
        }

        // The last sink may forward the message.
        return sinks[sink_count - 1]->on_message(
            m::tracing::may_forward_message_option::may_forward_message, env);
    }

    M_NOINLINE
    m::tracing::on_message_disposition
    multiplexor::handle_large_dispatch(std::unique_lock<std::mutex>& l,
                                       m::tracing::may_forward_message_option,
                                       m::tracing::envelope& env)
    {
        M_INTERNAL_ERROR_CHECK(l.owns_lock());

        std::vector<std::shared_ptr<sink_shim>> sinks;

        auto const sink_count = m_sink_shims.size();

        sinks.reserve(sink_count);
        auto backit = std::back_inserter(sinks);
        std::copy(m_sink_shims.begin(), m_sink_shims.end(), backit);

        l.unlock();

        // We could do the work to try to move one sink that could handle a forward
        // to the end of the list and try forwarding to it, but if we're really at
        // a point where we have a large number of sinks, maybe someone else will
        // have a hand at copying that code here. -@micgrier

        for (auto&& s : sinks)
        {
            auto const disp =
                s->on_message(m::tracing::may_forward_message_option::may_not_forward_message, env);
            M_INTERNAL_ERROR_CHECK(disp == m::tracing::on_message_disposition::message_processed);
        }

        return m::tracing::on_message_disposition::message_processed;
    }

    [[nodiscard]] bool
    multiplexor::could_forward_message(m::tracing::envelope const& env)
    {
        auto l = std::unique_lock(m_mutex);

        // If some sink could forward the message, then we could forward the message.

        for (auto&& s: m_sink_shims)
            if (s->could_forward_message(env))
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

        std::vector<std::shared_ptr<sink_shim>> new_sink_shims;

        for (auto&& e: m_channel_names)
            m_monitor->for_each_channel_sink(e, [&](auto snk) {
                if (!snk->closed())
                    new_sink_shims.emplace_back(snk);
            });

        // if we have multiple channels we might have duplicate entries for sinks, so we need to
        // now re-process the list of sinks to both:
        //
        // 1. remove any sinks that are closed
        // 2. remove any duplicates
        //
        // closed are easy. We will remove duplicates by sorting the
        // sinks by the raw values of the pointers and then look for
        // adjacent pointers.
        //
        // There's an algorithmic nightmare here if the list is long and
        // has many duplicates. In praactice the list is never long and
        // rarely has duplicates. The real trick is to put the nullptr
        // entries at the end so that when we reach them at the "compression"
        // phase of the operation, we can stop.
        //

        struct my_less
        {
            bool
            operator()(std::shared_ptr<sink_shim> const& l, std::shared_ptr<sink_shim> const& r)
            {
                auto inner_l = l->m_sink;
                auto inner_r = r->m_sink;

                auto lu = reinterpret_cast<uintptr_t>(inner_l.get());
                auto ru = reinterpret_cast<uintptr_t>(inner_r.get());

                return lu < ru;
            }
        };

        struct my_eq
        {
            bool
            operator()(std::shared_ptr<sink_shim> const& l, std::shared_ptr<sink_shim> const& r)
            {
                auto inner_l = l->m_sink;
                auto inner_r = r->m_sink;

                auto lu = reinterpret_cast<uintptr_t>(inner_l.get());
                auto ru = reinterpret_cast<uintptr_t>(inner_r.get());

                return lu == ru;
            }
        };

        //
        // Textbook use of sort/unique/erase
        //
        std::sort(new_sink_shims.begin(), new_sink_shims.end(), my_less{});
        auto last = std::unique(new_sink_shims.begin(), new_sink_shims.end(), my_eq{});
        new_sink_shims.erase(last, new_sink_shims.end());

        using std::swap;
        swap(m_sink_shims, new_sink_shims);
    }

    m::tracing::envelope
    multiplexor::allocate_message(m::tracing::event_kind kind)
    {
        return m_monitor->allocate_message(kind);
    }

    void
    multiplexor::deallocate_message(m::not_null<m::tracing::message*> msg) noexcept
    {
        m_monitor->deallocate_message(msg);
    }

} // namespace m::tracing_impl