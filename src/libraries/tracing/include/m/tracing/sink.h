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
#include <m/tracing/may_forward_message_option.h>
#include <m/tracing/message_processor.h>
#include <m/tracing/on_message_disposition.h>

using namespace m::string_view_literals;

namespace m
{
    namespace tracing
    {
        class monitor_class;
        class multiplexor;

        class sink : public message_processor
        {
        public:
            std::wstring
            name() const;

            // Determine if the call to on_message could copy so that
            // caller can make educated sense about order of calling
            // sinks. (Only one sink can forward so put the one that
            // could forward last.)
            virtual bool
            could_forward_message(envelope const& item) override = 0;

            virtual on_message_disposition
            on_message(may_forward_message_option may_forward_message, envelope& item) override = 0;

            virtual void
            close(close_flush_option cfo) noexcept override = 0;

        protected:
            sink(std::wstring_view name, m::not_null<monitor_class*> monitor);

            std::mutex                  m_mutex;
            std::wstring                m_name;
            m::not_null<monitor_class*> m_monitor;
            bool                        m_closed;

            friend class monitor_class;
            friend class multiplexor;
        };
    } // namespace tracing
} // namespace m
