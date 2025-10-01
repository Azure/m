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
#include <m/tracing/close_flush_option.h>
#include <m/tracing/event_kind.h>
#include <m/tracing/may_forward_message_option.h>
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
            virtual ~monitor_class() = default;

            virtual m::not_null<channel*>
            make_channel(m::wliteral_string_view name) = 0;

            virtual std::shared_ptr<source>
            make_source(event_kind kind = event_kind::information) = 0;

            virtual std::unique_ptr<sink_registration>
            register_sink(std::wstring_view channel_name, std::shared_ptr<sink> snk) = 0;

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
            virtual void
            close(close_flush_option cfo) noexcept = 0;

            virtual envelope
            duplicate_message(envelope const& item) = 0;

            virtual std::shared_ptr<multiplexor>
            get_multiplexor(std::initializer_list<std::wstring_view> channel_names) = 0;

            virtual topology_version
            get_topology_version() const = 0;

            [[nodiscard]] virtual envelope
            allocate_message(event_kind kind) override = 0;

            virtual void
            deallocate_message(m::not_null<tracing::imessage*> message) noexcept override = 0;
        };

        /// <summary>
        /// Allocates a new monitor class instance.
        /// </summary>
        /// <returns></returns>
        m::not_null<monitor_class*>
        make_monitor_class();
    } // namespace tracing
} // namespace m
