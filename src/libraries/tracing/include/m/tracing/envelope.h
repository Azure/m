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
#include <m/utility/pointers.h>

#include "event_context.h"
#include "event_kind.h"

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
            envelope(m::not_null<message*> msg);
            envelope(m::not_null<message*> msg, m::not_null<message_queue*> return_queue);
            envelope(envelope&& other) noexcept;
            void
            operator=(envelope&& other) noexcept;

            void
            reset();

            void
            reset(envelope const& other, m::not_null<message_queue*> return_queue);

            message*
            get_message() const;

            message_queue*
            get_message_queue() const;

            ~envelope();

        private:
            message*       m_message{};
            message_queue* m_return_queue{}; // may be nullptr
        };
    } // namespace tracing
} // namespace m
