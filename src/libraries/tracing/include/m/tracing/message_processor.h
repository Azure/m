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
#include <m/tracing/may_forward_message_option.h>
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
        /// performing work on them immediately, returning a message_processed status,
        /// or by further enqueuing them, returning a message_forwarded status.
        /// </summary>
        class message_processor
        {
        public:
            virtual ~message_processor() = default;

            virtual void
            close(close_flush_option cfo) = 0;

            [[nodiscard]] virtual on_message_disposition
            on_message(may_forward_message_option may_forward_message, envelope& env) = 0;

            [[nodiscard]] virtual bool
            could_forward_message(envelope const& env) = 0;
        };

    } // namespace tracing
} // namespace m
