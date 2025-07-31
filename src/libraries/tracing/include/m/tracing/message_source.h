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
#include <m/tracing/message.h>
#include <m/utility/pointers.h>

namespace m
{
    namespace tracing
    {
        /// <summary>
        /// The `message_source` class is a pure virtual interface type that
        /// is implemented by types that can be used to obtain "raw" empty
        /// `message` objects, encapsulated in `envelope` objects.
        /// </summary>
        class message_source
        {
        public:
            [[nodiscard]] virtual envelope
            allocate_message(event_kind kind) = 0;

            virtual void
            deallocate_message(m::not_null<tracing::message*> msg) noexcept = 0;

        protected:
            message_source()          = default;
            virtual ~message_source() = default;
        };

        void
        return_to_sender(envelope& env, m::not_null<message_source*> source);

        class message_allocator
        {
        public:
            message_allocator(m::not_null<message_source*> source, event_kind kind):
                m_source(source), m_envelope(source->allocate_message(kind)), m_armed(true)
            {}

            ~message_allocator()
            {
                if (m_armed)
                {
                    return_to_sender(m_envelope, m_source);
                }
            }

            envelope&
            env()
            {
                return m_envelope;
            }

            /// <summary>
            /// Returns the allocated envelope. Once this member function is called,
            /// the message_allocator is no longer available for use.
            /// </summary>
            /// <returns></returns>
            envelope&&
            move_env()
            {
                m_armed = false;
                return std::move(m_envelope);
            }

        private:
            m::not_null<message_source*> m_source;
            envelope                     m_envelope;
            bool                         m_armed;
        };
    } // namespace tracing
} // namespace m
