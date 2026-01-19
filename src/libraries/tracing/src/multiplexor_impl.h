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
#include <memory>
#include <mutex>
#include <new>
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
#include <m/tracing/multiplexor.h>
#include <m/tracing/on_message_disposition.h>
#include <m/tracing/sink.h>
#include <m/tracing/topology_version.h>
#include <m/utility/pointers.h>

#include "sink_shim.h"

using namespace m::string_view_literals;

namespace m::tracing_impl
{
    class message;
    class monitor;
    class multiplexor;

    //
    // A multiplexor ties some number of sources together with some number
    // of sinks.
    //
    class multiplexor :
        public m::tracing::multiplexor,
        public std::enable_shared_from_this<m::tracing_impl::multiplexor>
    {
    public:
        multiplexor(m::not_null<monitor*>                    monitor,
                    m::tracing::topology_version             topver,
                    std::initializer_list<std::wstring_view> channels);

        void
        close(m::tracing::close_flush_option cfo) noexcept override;

        [[nodiscard]] m::tracing::on_message_disposition
        on_message(m::tracing::may_forward_message_option may_forward_message,
                   m::tracing::envelope&                  item) override;

        [[nodiscard]] bool
        could_forward_message(m::tracing::envelope const& item) override;

        [[nodiscard]]
        m::tracing::envelope
        allocate_message(m::tracing::event_kind kind) override;

        void
        deallocate_message(m::not_null<m::tracing::imessage*> msg) noexcept override;

    private:
        void
        rebuild_topology_from_monitor() noexcept;

        /// <summary>
        /// Implementation of on_message() that only works for "small"
        /// topologies.
        /// </summary>
        /// <param name="may_forward_message"></param>
        /// <param name="item"></param>
        /// <returns></returns>
        m::tracing::on_message_disposition
        handle_large_dispatch(std::unique_lock<std::mutex>&          l,
                              m::tracing::may_forward_message_option may_forward_message,
                              m::tracing::envelope&                  item);

        // m_monitor and m_channel_names are not updated after construction
        // and thus are not guarded by m_mutex.
        m::not_null<m::tracing_impl::monitor*>  m_monitor;
        std::vector<std::wstring>               m_channel_names;
        std::mutex                              m_mutex;
        m::tracing::topology_version            m_topology_version;
        std::vector<std::shared_ptr<sink_shim>> m_sink_shims;
    };
} // namespace m::tracing_impl
