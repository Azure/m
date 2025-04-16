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

#include <gsl/gsl>

#include <m/strings/literal_string_view.h>

#include "envelope.h"
#include "event_context.h"
#include "event_kind.h"

using namespace m::string_view_literals;

namespace m
{
    namespace tracing
    {
        //
        // At its core, the tracing facility is a big queue processor.
        //
        // sources generate events which are directed at channels which
        // multiplex through filters and then end up at sinks.
        //
        // More complex topologies are imaginable; just consider the
        // ETW message handling or other message passing topologies like
        // DirectSound.
        //
        // The main concern is the size of the messages - how are they
        // allocated. For tracing the messages are constructed piecemeal
        // and we don't know the size beforehand, so either we need to
        // deal with arbitrary growth, or have some reasonable maximum
        // message size.
        //
        // For tracing we will pick some reasonable sized maximum message size
        // and have a queue of free messages available.
        //

        class message;
        class message_queue;

        class message_queue
        {
        public:
            message_queue();
            ~message_queue()                    = default;
            message_queue(message_queue const&) = delete;
            message_queue(message_queue&&)      = delete;
            void
            operator=(message_queue const&) = delete;

            bool
            empty() const;

            envelope
            try_dequeue();

            envelope
            dequeue();

            envelope
            wakeable_dequeue();

            void
            wake_waiters();

            void
            wait();

            void
            enqueue(gsl::not_null<message*> msg);

            void
            enqueue(envelope& e);

        private:
            mutable std::mutex      m_mutex;
            std::condition_variable m_cv;
            std::size_t             m_waiter_count;
            std::queue<envelope>    m_queue;
        };
    } // namespace tracing
} // namespace m
