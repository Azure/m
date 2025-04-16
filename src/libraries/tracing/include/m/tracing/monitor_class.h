// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <format>
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

#include <m/utility/locked.h>

#include "channel.h"
#include "envelope.h"
#include "event_kind.h"
#include "message_queue.h"
#include "on_message_disposition.h"
#include "sink.h"
#include "source.h"
#include "topology_version.h"

namespace m
{
    namespace tracing
    {
        class monitor_class;
        class multiplexor;

        class monitor_class
        {
        public:
            monitor_class();
            ~monitor_class();

            m::not_null<channel*>
            make_channel(std::wstring_view name);

            std::shared_ptr<source>
            make_source(event_kind kind = event_kind::information);

            std::shared_ptr<source>
            make_source(event_kind kind, std::initializer_list<std::wstring_view> channels);

            void
            register_sink(std::shared_ptr<sink> snk);

            envelope
            copy_message(m::locked_t, envelope const& item);

            envelope
            copy_message(envelope const& item);

            template <typename Callable, typename... Types>
            void
            for_each_channel_sink(m::locked_t,
                                  std::wstring_view channel_name,
                                  Callable          callable,
                                  Types&&... args)
            {
                // Just grab this from the collection instead of
                // referring to it by name so that if the collection
                // changes, we adapt.
                auto const lt = m_channel_sinks.key_comp();

                auto it = m_channel_sinks.lower_bound(channel_name);

                // Until we hit a key that's larger than the channel name,
                // go through the list calling the callable passing the
                // value and then the args.
                while ((it != m_channel_sinks.end()) && !lt(it->first, channel_name))
                {
                    std::invoke(callable, it->second, std::forward<Types>(args)...);
                    it++;
                }
            }

            template <typename Callable, typename... Types>
            void
            for_each_channel_sink(std::wstring_view channel_name,
                                  Callable          callable,
                                  Types&&... args)
            {
                auto l = std::unique_lock(m_mutex);
                for_each_channel_sink(
                    m::locked, channel_name, callable, std::forward<Types>(args)...);
            }

            std::shared_ptr<multiplexor>
            get_multiplexor(std::initializer_list<std::wstring_view> sink_names);

            topology_version
            get_topology_version() const;

            envelope
            reserve_message();

        private:
            std::atomic<topology_version>                                   m_topology_version;
            std::mutex                                                      m_mutex;
            std::map<std::wstring, std::unique_ptr<channel>, std::less<>>   m_channels;
            std::multimap<std::wstring, std::shared_ptr<sink>, std::less<>> m_channel_sinks;
            std::vector<std::shared_ptr<sink>>                              m_sinks;
            message_queue                                                   m_message_queue;
            std::unique_ptr<message[]>                                      m_raw_messages;

            friend class multiplexor;
        };
    } // namespace tracing
} // namespace m
