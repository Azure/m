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

#include <m/utility/pointers.h>

#include "envelope.h"
#include "message_queue.h"
#include "sink.h"
#include "tracing.h"

namespace m
{
    namespace tracing
    {
        class cout_sink : public sink
        {
        public:
            cout_sink(m::not_null<monitor_class*> monitor);

            // Kind of hokey but who is responsible for registering the
            // cout based sink? This is how it's done I guess
            static void
            register_sink(m::not_null<monitor_class*> monitor);

        protected:
            on_message_disposition
            on_message(may_queue_option may_queue, envelope& env) override;

            bool would_queue(envelope const&) override;

            void
            close() override;

        private:
            message_queue                                         m_message_queue;
            std::thread                                           m_thread;
            bool                                                  m_done;
            static inline std::atomic<std::shared_ptr<cout_sink>> ms_cout_sink;

            void
            sink_thread();
        };

    } // namespace tracing
} // namespace m
