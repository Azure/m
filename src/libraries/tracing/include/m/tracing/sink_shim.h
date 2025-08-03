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
#include <m/tracing/close_flush_option.h>
#include <m/tracing/envelope.h>
#include <m/tracing/may_queue_option.h>
#include <m/tracing/message_processor.h>
#include <m/tracing/on_message_disposition.h>
#include <m/tracing/sink.h>

using namespace m::string_view_literals;

namespace m::tracing::internal
{
    class monitor_class;
    class multiplexor;

    /// <summary>
    /// The `sink_shim` struct is a "wrapper" around how the tracing
    /// infrastructure interacts with client-provided shim implementations.
    ///
    /// When shims are "unregistered", it is vital that the tracing
    /// infrastructure hold no further references on to the shim so
    /// that the client is free to deallocate memory, since on
    /// platforms that support it, they may be unloading the code
    /// backing the virtual member functions.
    ///
    /// Thus m::tracing _always_ uses the sink_shim type which fronts
    /// for the m::shim "interface". When the sink_shim gets a `close()`
    /// call, it calls `.reset()` on the `std::shared_ptr<>` within
    /// and then the infrastructure is done with it.
    ///
    /// The shim's `close()` member function is called when the
    /// `sink_registration_impl` object's destructor runs.
    /// </summary>
    class sink_shim : public message_processor
    {
    public:
        sink_shim();
        sink_shim(std::shared_ptr<sink> const& s);
        sink_shim(sink_shim&& other) noexcept;
        ~sink_shim() = default;
        sink_shim&
        operator=(sink_shim const& other) = delete;
        sink_shim&
        operator=(sink_shim&& other) noexcept;

        void
        close(close_flush_option cfo) noexcept override;

        on_message_disposition
        on_message(may_queue_option may_queue, envelope& item) override;

        bool
        would_queue(envelope const& item) override;

    private:
        std::mutex            m_mutex;
        std::shared_ptr<sink> m_sink;
        bool                  m_closed; // debate between using !m_sink as closed vs. separate bool
    };
} // namespace m::tracing::internal
