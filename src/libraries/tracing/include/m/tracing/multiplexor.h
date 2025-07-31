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

#include <m/strings/literal_string_view.h>
#include <m/tracing/channel.h>
#include <m/tracing/envelope.h>
#include <m/tracing/event_kind.h>
#include <m/tracing/message_queue.h>
#include <m/tracing/message_source.h>
#include <m/tracing/on_message_disposition.h>
#include <m/tracing/sink.h>
#include <m/tracing/sink_shim.h>
#include <m/tracing/topology_version.h>
#include <m/utility/pointers.h>

using namespace m::string_view_literals;

namespace m
{
    namespace tracing
    {
        class monitor_class;
        class multiplexor;

        //
        // A multiplexor ties some number of sources together with some number
        // of sinks.
        //
        class multiplexor : public message_source
        {
        public:
            multiplexor(m::not_null<monitor_class*>              monitor,
                        topology_version                         topver,
                        std::initializer_list<std::wstring_view> channels);

            [[nodiscard]] on_message_disposition
            on_message(envelope& item);

            [[nodiscard]]
            envelope
            allocate_message(event_kind kind) override;

        private:
            // m_monitor and m_channel_names are not updated after construction
            m::not_null<monitor_class*> m_monitor;
            std::vector<std::wstring>   m_channel_names;
            std::mutex                  m_mutex;
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-private-field"
#endif
            topology_version m_topology_version;
#ifdef __clang__
#pragma clang diagnostic pop
#endif
            std::vector<std::shared_ptr<internal::sink_shim>> m_sink_shims;
        };
    } // namespace tracing
} // namespace m
