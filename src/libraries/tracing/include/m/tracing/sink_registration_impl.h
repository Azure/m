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
#include <m/tracing/envelope.h>
#include <m/tracing/may_queue_option.h>
#include <m/tracing/on_message_disposition.h>
#include <m/tracing/sink_registration.h>
#include <m/tracing/sink_shim.h>

using namespace m::string_view_literals;

namespace m::tracing::internal
{
    class monitor_class;
    class multiplexor;

    struct sink_registration_impl final : sink_registration
    {
        sink_registration_impl(std::shared_ptr<sink_shim> const& shim);
        sink_registration_impl(sink_registration_impl&& other);
        ~sink_registration_impl();
        std::shared_ptr<sink_shim> m_sink_shim;
    };

} // namespace m::tracing::internal
