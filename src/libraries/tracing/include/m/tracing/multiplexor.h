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
#include <m/tracing/message_processor.h>
#include <m/tracing/message_queue.h>
#include <m/tracing/message_source.h>
#include <m/tracing/on_message_disposition.h>
#include <m/tracing/sink.h>
#include <m/tracing/topology_version.h>
#include <m/utility/pointers.h>

using namespace m::string_view_literals;

namespace m::tracing
{
    class message;
    class monitor_class;
    class multiplexor;

    //
    // A multiplexor ties some number of sources together with some number
    // of sinks.
    //
    class multiplexor : public message_source, public message_processor
    {
    public:
        virtual ~multiplexor() = default;

        virtual void
        close(close_flush_option cfo) noexcept override = 0;

        [[nodiscard]] virtual on_message_disposition
        on_message(may_forward_message_option may_forward_message, envelope& item) override = 0;

        [[nodiscard]] virtual bool
        could_forward_message(envelope const& item) override = 0;

        [[nodiscard]] virtual envelope
        allocate_message(event_kind kind) override = 0;

        virtual void
        deallocate_message(m::not_null<tracing::message*> msg) noexcept override = 0;
    };
} // namespace m::tracing
