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

#include "envelope.h"
#include "message_queue.h"
#include "on_message_disposition.h"

using namespace m::string_view_literals;

namespace m
{
    namespace tracing
    {
        class monitor_class;
        class multiplexor;

        class sink
        {
        public:
            std::wstring
            name() const;

        protected:
            sink(std::wstring_view name, m::not_null<monitor_class*> monitor);

            // Determine if the call to on_message would copy so that
            // caller knows whether to make a copy of the message if
            // there is more than one sink
            virtual bool
            would_queue(envelope const& item) = 0;

            enum class may_queue_option
            {
                may_queue,
                may_not_queue,
            };

            virtual on_message_disposition
            on_message(may_queue_option may_queue, envelope& item) = 0;

            virtual void
            close() = 0;

            std::mutex                    m_mutex;
            std::wstring                  m_name;
            m::not_null<monitor_class*> m_monitor;
            bool                          m_closed;

            friend class monitor_class;
            friend class multiplexor;
        };
    } // namespace tracing
} // namespace m
