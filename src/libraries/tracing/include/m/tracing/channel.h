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

#include "event_kind.h"
#include "message.h"
#include "message_queue.h"
#include "monitor_class.h"
#include "monitor_class_var.h"
#include "on_message_disposition.h"
#include "sink.h"

using namespace m::string_view_literals;

namespace m
{
    namespace tracing
    {
        class monitor_class;
        class multiplexor;

        class channel
        {
        public:
            channel(std::wstring_view name);

        private:
            std::mutex           m_mutex;
            std::wstring         m_name;

            friend class multiplexor;
        };

        inline constexpr auto diagnostic_channel_name = L"diagnostic"_sl;

        inline auto diagnostic_channel = monitor.make_channel(diagnostic_channel_name);

        // inline auto operational_channel = monitor.make_channel(L"operational"_sl);
    } // namespace tracing
} // namespace m
