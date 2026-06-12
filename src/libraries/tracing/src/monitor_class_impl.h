// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <format>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <m/pool/pool.h>
#include <m/string_buffer/string_buffer.h>
#include <m/strings/literal_string_view.h>
#include <m/tracing/channel.h>
#include <m/tracing/close_flush_option.h>
#include <m/tracing/envelope.h>
#include <m/tracing/event_kind.h>
#include <m/tracing/may_forward_message_option.h>
#include <m/tracing/message.h>
#include <m/tracing/message_queue.h>
#include <m/tracing/monitor_class.h>
#include <m/tracing/multiplexor.h>
#include <m/tracing/on_message_disposition.h>
#include <m/tracing/sink.h>
#include <m/tracing/sink_registration.h>
#include <m/tracing/source.h>
#include <m/tracing/topology_version.h>
#include <m/utility/locked.h>

#include "sink_shim.h"

using namespace m::string_view_literals;

namespace m::tracing_impl
{
    class monitor : public m::tracing::monitor_class
    {
    public:
        monitor();
        ~monitor();

        m::not_null<m::tracing::channel*>
        make_channel(m::wliteral_string_view name) override;

        std::shared_ptr<m::tracing::source>
        make_source(m::tracing::event_kind kind) override;

        std::unique_ptr<m::tracing::sink_registration>
        register_sink(std::wstring_view                 channel_name,
                      std::shared_ptr<m::tracing::sink> snk) override;

        /// <summary>
        /// The `close` with `emergency_stop` == `true` member function
        /// should be called
        /// very judiciously when a client needs to shut down the
        /// tracing system because `std::abort()` or similar is
        /// about to be invoked.
        ///
        /// Usually `close()` should never be called. It should proably
        /// never be used outside of the `emergency_stop == true` case.
        /// </summary>
        /// <param name="emergency_stop">Pass `true` to request all queues
        /// to flush immediately on the foreground, `false` to allow operations
        /// to proceed with less haste.</param>
        void
        close(m::tracing::close_flush_option cfo) noexcept override;

        m::tracing::envelope
        duplicate_message(m::tracing::envelope const& item) override;

        std::shared_ptr<m::tracing::multiplexor>
        get_multiplexor(std::initializer_list<std::wstring_view> channel_names) override;

        m::tracing::topology_version
        get_topology_version() const override;

        [[nodiscard]] m::tracing::envelope
        allocate_message(m::tracing::event_kind kind) override;

        void
        deallocate_message(m::not_null<tracing::imessage*> message) noexcept override;

        m::tracing::envelope
        duplicate_message(m::locked_t, m::tracing::envelope const& item);

        template <typename Callable, typename... Types>
        void
        for_each_channel_sink(m::locked_t,
                              std::wstring_view channel_name,
                              Callable&&        callable,
                              Types const&... args)
        {
            // Just grab this from the collection instead of
            // referring to it by name so that if the collection
            // changes, we adapt.
            auto const key_comp = m_channel_sink_shims.key_comp();

            auto it = m_channel_sink_shims.lower_bound(channel_name);

            // Until we hit a key that's larger than the channel name,
            // go through the list calling the callable passing the
            // value and then the args.
            while ((it != m_channel_sink_shims.end()) && !key_comp(it->first, channel_name))
            {
                auto&& sink = it->second;

                if (sink->closed())
                    m_closed_sinks = true;

                std::invoke(callable, std::forward<decltype(sink)>(sink), args...);
                it++;
            }
        }

        template <typename Callable, typename... Types>
        void
        for_each_channel_sink(std::wstring_view channel_name,
                              Callable&&        callable,
                              Types const&... args)
        {
            auto l = std::unique_lock(m_mutex);
            for_each_channel_sink(m::locked, channel_name, callable, args...);
        }

    private:
        using channel_map_type =
            std::map<std::wstring, std::unique_ptr<m::tracing::channel>, std::less<>>;

        using channel_sink_shim_map_type =
            std::multimap<std::wstring, std::shared_ptr<sink_shim>, std::less<>>;

        // Number of preallocated message slots backing m_message_queue.
        static constexpr std::size_t raw_message_count = 64;

        // Raw storage for the message slots. The messages take a pool argument so
        // they cannot be default-constructed as an array; instead we allocate this
        // correctly-aligned byte block and placement-new each message into it. Keeping
        // the block in a typed unique_ptr lets the destructor free it through the same
        // type it was allocated with rather than through the message* alias.
        struct message_array_type
        {
            alignas(m::tracing::message)
                std::array<std::byte, raw_message_count * sizeof(m::tracing::message)> m_data;
        };

        std::atomic<m::tracing::topology_version> m_topology_version;
        std::mutex                                m_mutex;
        channel_map_type                          m_channels;
        channel_sink_shim_map_type                m_channel_sink_shims;
        std::vector<std::shared_ptr<sink_shim>>   m_sink_shims;
        m::tracing::message_queue                 m_message_queue;
        std::unique_ptr<message_array_type>       m_message_storage;
        m::tracing::message*                      m_raw_messages{};
        bool                                              m_closed_sinks;
        std::shared_ptr<wpooled_string_buffer::pool_type> m_pool;

        friend class multiplexor;
    };
} // namespace m::tracing_impl
