// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>

#include <m/debugging/dbg_format.h>
#include <m/tracing/message.h>
#include <m/tracing/monitor_class.h>
#include <m/tracing/multiplexor.h>
#include <m/tracing/tracing.h>
#include <m/utility/pointers.h>

#include "monitor_class_impl.h"
#include "multiplexor_impl.h"
#include "sink_registration_impl.h"
#include "sink_shim.h"

namespace m::tracing_impl
{
    //
    monitor::monitor(): m_closed_sinks{false}
    {
        m_pool = std::make_shared<wpooled_string_buffer::pool_type>();

        m::dbg_format("Constructing monitor at {}", reinterpret_cast<uintptr_t>(this));
        constexpr std::size_t raw_message_count = 64;

        // It's tricky to construct the messages since each takes a pool. We have to allocate
        // the storage, then construct them.

        using message_array_type = /* alignas(m::tracing::message) */ std::array<std::byte, raw_message_count * sizeof(m::tracing::message)>;

        auto p = new message_array_type;

        m_raw_messages = reinterpret_cast<m::tracing::message*>(p);

        for (std::size_t i = 0; i < raw_message_count; i++)
        {
            ::new (&m_raw_messages[i]) m::tracing::message(m_pool);
        }

        for (std::size_t i = 0; i < raw_message_count; i++)
        {
            m::tracing::envelope item(m::not_null(this), &m_raw_messages[i]);
            m_message_queue.enqueue(item);
        }
    }

    monitor::~monitor()
    {
        for (auto&& s: m_sink_shims)
            s->close(m::tracing::close_flush_option::normal);
    }

    m::not_null<m::tracing::channel*>
    monitor::make_channel(m::wliteral_string_view name)
    {
        auto l = std::unique_lock(m_mutex);

        auto it = m_channels.find(name);
        if (it == m_channels.end())
        {
            auto r = m_channels.emplace(
                std::make_pair(name, std::make_unique<m::tracing::channel>(name)));
            return r.first->second.get();
        }

        return (*it).second.get();
    }

    std::shared_ptr<m::tracing::source>
    monitor::make_source(m::tracing::event_kind kind)
    {
        return std::make_shared<m::tracing::source>(
            this, kind, m::tracing::diagnostic_channel_name);
    }

    void
    monitor::close(m::tracing::close_flush_option cfo) noexcept
    {
        auto l = std::unique_lock(m_mutex);

        for (auto&& s: m_sink_shims)
            s->close(cfo);
    }

    std::shared_ptr<m::tracing::multiplexor>
    monitor::get_multiplexor(std::initializer_list<std::wstring_view> channel_names)
    {
        auto topver = m_topology_version.load(std::memory_order_relaxed);
        auto l      = std::unique_lock(m_mutex);
        // Just create the multiplexor with the channel names.
        //
        // Multiplexors should deal with changing topologies (eventually)
        // so there's no reason to deal with computing the initial topology
        // here.
        //

        return std::make_shared<m::tracing_impl::multiplexor>(this, topver, channel_names);
    }

    m::tracing::topology_version
    monitor::get_topology_version() const
    {
        return m_topology_version.load(std::memory_order_relaxed);
    }

    m::tracing::envelope
    monitor::allocate_message(m::tracing::event_kind kind)
    {
        auto l   = std::unique_lock(m_mutex);
        auto env = m_message_queue.dequeue(); // don't make env const to allow rvo
        env.message()->kind(kind);
        return env;
    }

    void
    monitor::deallocate_message(m::not_null<m::tracing::imessage*> ptr) noexcept
    {
        auto l = std::unique_lock(m_mutex);
        m_message_queue.enqueue(ptr);
    }

    m::tracing::envelope
    monitor::duplicate_message(locked_t, m::tracing::envelope const& item_in)
    {
        auto item_copy = m_message_queue.dequeue();

        item_in.message()->copy_into(item_copy.message());

        return item_copy;
    }

    m::tracing::envelope
    monitor::duplicate_message(m::tracing::envelope const& item_in)
    {
        auto l = std::unique_lock(m_mutex);
        return duplicate_message(locked, item_in);
    }

    std::unique_ptr<m::tracing::sink_registration>
    monitor::register_sink(std::wstring_view channel_name, std::shared_ptr<m::tracing::sink> snk)
    {
        // Let's initially just register whatever sinks we get with the
        // diagnostic channel to get things rolling
        auto l = std::unique_lock(m_mutex);

        // Elaborate increment of the topology version since we're changing the shape
        // of the graph. If another thread reads this, they will block on the mutex
        // to read and rebuild the actual new topology.
        m_topology_version.store(m_topology_version.load(std::memory_order_relaxed) + 1,
                                 std::memory_order_relaxed);

        auto shim = std::make_shared<sink_shim>(snk);
        auto return_value =
            std::unique_ptr<m::tracing::sink_registration>(new sink_registration_impl(shim));
        auto const insertion_it = m_channel_sink_shims.insert(std::make_pair(channel_name, shim));

        // It would be nice to have an RAII type to un-insert but RAII-ifying literally
        // everything can be tedious.
        try
        {
            m_sink_shims.push_back(shim);
        }
        catch (std::exception const&)
        {
            m_channel_sink_shims.erase(insertion_it);
            throw;
        }

        return return_value;
    }

} // namespace m::tracing_impl

namespace m::tracing
{
    m::not_null<monitor_class*>
    make_monitor_class()
    {
        return new m::tracing_impl::monitor();
    }
} // namespace m::tracing
