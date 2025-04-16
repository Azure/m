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

#include <gsl/gsl>

#include <m/strings/literal_string_view.h>

#include "channel.h"
#include "envelope.h"
#include "event_kind.h"
#include "message_queue.h"
#include "monitor_class.h"
#include "on_message_disposition.h"
#include "sink.h"
#include "source.h"
#include "topology_version.h"

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
        class multiplexor
        {
        public:
            multiplexor(gsl::not_null<monitor_class*>            monitor,
                        topology_version                         topver,
                        std::initializer_list<std::wstring_view> channels);

            [[nodiscard]] on_message_disposition
            on_message(envelope& item);

            [[nodiscard]]
            envelope
            reserve_message();

        private:
            // m_monitor and m_channel_names are not updated after construction
            gsl::not_null<monitor_class*> m_monitor;
            std::vector<std::wstring>     m_channel_names;

            std::mutex                         m_mutex;
            topology_version                   m_topology_version;
            std::vector<std::shared_ptr<sink>> m_sinks;
        };
    } // namespace tracing
} // namespace m
