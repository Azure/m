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

#include <m/tracing/envelope.h>
#include <m/tracing/event_kind.h>
#include <m/tracing/message_source.h>
#include <m/tracing/message_processor.h>
#include <m/utility/pointers.h>

namespace m
{
    namespace tracing
    {
        class message_allocator
        {
        public:
            message_allocator(m::not_null<message_source*> source, event_kind kind);

            ~message_allocator();

            envelope&
            env();

            m::not_null<tracing::message*>
            message();

            /// <summary>
            /// Returns the allocated envelope. Once this member function is called,
            /// the message_allocator is no longer available for use.
            /// </summary>
            /// <returns></returns>
            envelope&&
            move_env();

            /// <summary>
            /// Sends the message. If the message operation is
            /// completed, will return the
            /// it to its source.
            /// </summary>
            void
            send_message(m::not_null<message_processor*> processor);

            /// <summary>
            /// The `release()` member function causes the `message_allocator` object
            /// to not deallocate the allocated message, if there is an
            /// allocated message.
            /// 
            /// The use case for this is when a message is forwarded and then the
            /// recipient responded that they had forwarded the message on to
            /// another recipient for processing, so that the original
            /// sender no longer owned the message.
            /// </summary>
            void
            release();

        private:
            envelope                     m_envelope;
            bool                         m_armed;
        };
    } // namespace tracing
} // namespace m
