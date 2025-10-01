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
#include <m/tracing/imessage.h>
#include <m/tracing/message_processor.h>
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
            [[nodiscard]] virtual tracing::envelope
            allocate_message(event_kind kind) = 0;

            virtual void
            deallocate_message(m::not_null<tracing::imessage*> msg) noexcept = 0;

        protected:
            message_source()          = default;
            virtual ~message_source() = default;
        };

        void
        return_to_sender(tracing::envelope const& env);
    } // namespace tracing
} // namespace m
