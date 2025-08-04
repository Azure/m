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
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <m/strings/literal_string_view.h>
#include <m/tracing/close_flush_option.h>
#include <m/tracing/envelope.h>
#include <m/tracing/message_queue.h>
#include <m/tracing/sink.h>
#include <m/tracing/tracing.h>

using namespace m::string_view_literals;

namespace m
{
    namespace tracing
    {
        class cout_sink : public sink
        {
        public:
            cout_sink(m::not_null<monitor_class*> monitor);
            virtual ~cout_sink() {}

            // Kind of hokey but who is responsible for registering the
            // cout based sink? This is how it's done I guess
            static std::unique_ptr<sink_registration>
            register_sink(std::wstring_view channel_name, m::not_null<monitor_class*> monitor);

        protected:
            on_message_disposition
            on_message(may_forward_message_option may_forward_message, envelope& env) override;

            bool
            could_forward_message(envelope const&) override;

            void
            close(close_flush_option cfo) noexcept override;

        private:
            message_queue                                         m_message_queue;
            std::atomic<bool>                                     m_done;
            std::atomic<bool>                                     m_stop;
            std::thread                                           m_thread;
            static inline std::atomic<std::shared_ptr<cout_sink>> ms_cout_sink;

            void
            process_message(message* msg);

            void
            sink_thread();
        };

    } // namespace tracing
} // namespace m
