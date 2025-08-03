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
#include <m/tracing/may_queue_option.h>
#include <m/tracing/close_flush_option.h>
#include <m/tracing/on_message_disposition.h>

namespace m
{
    namespace tracing
    {
        /// <summary>
        /// The `message_processor` class is an abstract class of pure virtual
        /// member functions.
        /// 
        /// Implementors of the class handle tracing messages either by
        /// performing work on them immediately, returning a completed status,
        /// or by further enqueuing them, returning a queued status.
        /// </summary>
        class message_processor
        {
        public:
            virtual ~message_processor() = default;

            virtual void
            close(close_flush_option cfo) = 0;

            [[nodiscard]] virtual on_message_disposition
            on_message(may_queue_option may_queue, envelope& env) = 0;

            [[nodiscard]] virtual bool
            would_queue(envelope const& env) = 0;
        };

    } // namespace tracing
} // namespace m
