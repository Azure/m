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
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <m/tracing/envelope.h>
#include <m/tracing/multiplexor.h>
#include <m/tracing/sink.h>
#include <m/tracing/source.h>

#include <m/tracing/channel.h>

#include <m/strings/literal_string_view.h>
#include <m/tracing/event_kind.h>
#include <m/tracing/may_queue_option.h>
#include <m/tracing/message_queue.h>
#include <m/tracing/on_message_disposition.h>
#include <m/tracing/sink.h>
#include <m/tracing/sink_registration.h>
#include <m/tracing/topology_version.h>
#include <m/utility/locked.h>

using namespace m::string_view_literals;

namespace m
{
    namespace tracing
    {
        class multiplexor;

        class monitor_class : public message_source
        {
        public:
            monitor_class();
            ~monitor_class();

            m::not_null<channel*>
            make_channel(m::wliteral_string_view name);

            std::shared_ptr<source>
            make_source(event_kind kind = event_kind::information);

            std::unique_ptr<sink_registration>
            register_sink(std::shared_ptr<sink> snk);

            envelope
            copy_message(m::locked_t, envelope const& item);

            envelope
            copy_message(envelope const& item);

            template <typename Callable, typename... Types>
            void
            for_each_channel_sink(m::locked_t,
                                  std::wstring_view channel_name,
                                  Callable&&        callable,
                                  Types&&... args)
            {
                // Just grab this from the collection instead of
                // referring to it by name so that if the collection
                // changes, we adapt.
                auto const lt = m_channel_sink_shims.key_comp();

                auto it = m_channel_sink_shims.lower_bound(channel_name);

                // Until we hit a key that's larger than the channel name,
                // go through the list calling the callable passing the
                // value and then the args.
                while ((it != m_channel_sink_shims.end()) && !lt(it->first, channel_name))
                {
                    std::invoke(callable, it->second, args...);
                    it++;
                }
            }

            template <typename Callable, typename... Types>
            void
            for_each_channel_sink(std::wstring_view channel_name,
                                  Callable&&        callable,
                                  Types&&... args)
            {
                auto l = std::unique_lock(m_mutex);
                for_each_channel_sink(m::locked, channel_name, callable, args...);
            }

            std::shared_ptr<multiplexor>
            get_multiplexor(std::initializer_list<std::wstring_view> sink_names);

            topology_version
            get_topology_version() const;

            [[nodiscard]] envelope
            allocate_message(event_kind kind) override;

            void
            deallocate_message(m::not_null<tracing::message*> message) override;

        private:
            using channel_map_type = std::map<std::wstring, std::unique_ptr<channel>, std::less<>>;
            using channel_sink_shim_map_type =
                std::multimap<std::wstring, std::shared_ptr<internal::sink_shim>, std::less<>>;

            std::atomic<topology_version>                     m_topology_version;
            std::mutex                                        m_mutex;
            channel_map_type                                  m_channels;
            channel_sink_shim_map_type                        m_channel_sink_shims;
            std::vector<std::shared_ptr<internal::sink_shim>> m_sink_shims;
            message_queue                                     m_message_queue;
            std::unique_ptr<message[]>                        m_raw_messages;

            friend class multiplexor;
        };
    } // namespace tracing
} // namespace m
