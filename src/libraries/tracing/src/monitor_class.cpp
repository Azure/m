// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>

#include <m/tracing/monitor_class.h>
#include <m/tracing/sink_registration_impl.h>
#include <m/tracing/tracing.h>
#include <m/utility/pointers.h>

namespace m::tracing
{
    //
    monitor_class::monitor_class()
    {
        constexpr std::size_t raw_message_count = 64;
        m_raw_messages                          = std::make_unique<message[]>(raw_message_count);

        for (std::size_t i = 0; i < raw_message_count; i++)
            m_message_queue.enqueue(m::not_null(&m_raw_messages[i], m::not_null(this)));
    }

    monitor_class::~monitor_class()
    {
        for (auto&& s: m_sink_shims)
            s->close();
    }

    m::not_null<channel*>
    monitor_class::make_channel(m::wliteral_string_view name)
    {
        auto l = std::unique_lock(m_mutex);

        auto it = m_channels.find(name);
        if (it == m_channels.end())
        {
            auto r = m_channels.emplace(std::make_pair(name, std::make_unique<channel>(name)));
            return r.first->second.get();
        }

        return (*it).second.get();
    }

    std::shared_ptr<source>
    monitor_class::make_source(event_kind kind)
    {
        return std::make_shared<source>(this, kind, diagnostic_channel_name);
    }

    std::shared_ptr<multiplexor>
    monitor_class::get_multiplexor(std::initializer_list<std::wstring_view> channel_names)
    {
        auto topver = m_topology_version.load(std::memory_order_relaxed);
        auto l      = std::unique_lock(m_mutex);
        // Just create the multiplexor with the channel names.
        //
        // Multiplexors should deal with changing topologies (eventually)
        // so there's no reason to deal with computing the initial topology
        // here.
        //
        return std::make_shared<multiplexor>(this, topver, channel_names);
    }

    topology_version
    monitor_class::get_topology_version() const
    {
        return m_topology_version.load(std::memory_order_relaxed);
    }

    envelope
    monitor_class::allocate_message(event_kind kind)
    {
        auto l   = std::unique_lock(m_mutex);
        auto env = m_message_queue.dequeue(); // don't make env const to allow rvo
        env.message()->kind(kind);
        return env;
    }

    void
        monitor_class::deallocate_message(m::not_null<message*> ptr)
    {
        auto l = std::unique_lock(m_mutex);
        m_message_queue.enqueue(e);
    }

    envelope
    monitor_class::copy_message(locked_t, envelope const& item_in)
    {
        auto item_copy = m_message_queue.dequeue();
        item_copy.reset(item_in, &m_message_queue);
        return item_copy;
    }

    envelope
    monitor_class::copy_message(envelope const& item_in)
    {
        auto l = std::unique_lock(m_mutex);
        return copy_message(locked, item_in);
    }

    std::unique_ptr<sink_registration>
    monitor_class::register_sink(std::shared_ptr<sink> snk)
    {
        // Let's initially just register whatever sinks we get with the
        // diagnostic channel to get things rolling
        auto l            = std::unique_lock(m_mutex);
        auto shim         = std::make_shared<internal::sink_shim>(snk);
        auto return_value = new internal::sink_registration_impl(shim);
        m_channel_sink_shims.insert(std::make_pair(diagnostic_channel_name, shim));
        m_sink_shims.push_back(shim);
        return std::unique_ptr<sink_registration>(return_value);
    }

} // namespace m::tracing