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
#include <print>
#include <queue>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <m/strings/literal_string_view.h>
#include <m/tracing/channel.h>
#include <m/tracing/event_kind.h>
#include <m/tracing/message_queue.h>
#include <m/tracing/monitor_class.h>
#include <m/tracing/monitor_class_var.h>
#include <m/tracing/multiplexor.h>
#include <m/tracing/on_message_disposition.h>
#include <m/tracing/sink.h>
#include <m/tracing/source.h>

using namespace m::string_view_literals;

namespace m::tracing
{
    inline auto src = monitor->make_source();

    inline constexpr auto diagnostic_channel_name = L"diagnostic"_sl;

    inline auto diagnostic_channel = monitor->make_channel(diagnostic_channel_name);

    // inline auto operational_channel = monitor->make_channel(L"operational"_sl);
}

namespace m
{
    template <typename... Types>
    void
    wtrace(tracing::event_kind kind, const std::wformat_string<Types...> fmt, Types&&... args)
    {
        m::tracing::src->wlog(kind, fmt, std::forward<Types>(args)...);
    }

    template <typename... Types>
    void
    wtrace(const std::wformat_string<Types...> fmt, Types&&... args)
    {
        tracing::src->wlog(fmt, std::forward<Types>(args)...);
    }

    template <typename... Types>
    void
    wtrace_error(const std::wformat_string<Types...> fmt, Types&&... args)
    {
        tracing::src->wlog(tracing::event_kind::error, fmt, std::forward<Types>(args)...);
    }

    template <typename... Types>
    void
    trace(tracing::event_kind kind, const std::format_string<Types...> fmt, Types&&... args)
    {
        m::tracing::src->log(kind, fmt, std::forward<Types>(args)...);
    }

    template <typename... Types>
    void
    trace(const std::format_string<Types...> fmt, Types&&... args)
    {
        tracing::src->log(fmt, std::forward<Types>(args)...);
    }

    template <typename... Types>
    void
    trace_error(const std::format_string<Types...> fmt, Types&&... args)
    {
        tracing::src->log(tracing::event_kind::error, fmt, std::forward<Types>(args)...);
    }

} // namespace m