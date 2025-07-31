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
#include <m/tracing/event_context.h>
#include <m/tracing/event_kind.h>
#include <m/tracing/message_source.h>
#include <m/utility/pointers.h>

using namespace m::string_view_literals;

namespace m
{
    namespace tracing
    {
        class message;
        class message_queue;

        class envelope
        {
        public:
            envelope() = default;
            envelope(m::not_null<message*> msg, m::not_null<message_source*> source);

            envelope(envelope const& other) = delete;
            envelope(envelope&& other) noexcept;

            void
            operator=(envelope const& other) = delete;

            void
            operator=(envelope&& other) noexcept;

            void
            swap(envelope& other) noexcept;

            void
            reset();

            void
            reset(envelope const& other, m::not_null<message_queue*> return_queue);

            message*
            message() const;

            message_queue*
            message_queue() const;

            ~envelope();

        private:
            tracing::message*            m_message{};
            m::not_null<message_source*> m_message_source{};
        };
    } // namespace tracing
} // namespace m
