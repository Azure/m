// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <format>
#include <initializer_list>
#include <iterator>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <m/cast/to.h>
#include <m/strings/literal_string_view.h>
#include <m/tracing/debugging.h>
#include <m/tracing/event_context.h>
#include <m/tracing/event_kind.h>
#include <m/tracing/safe_array_iterator.h>
#include <m/utility/pointers.h>

using namespace m::string_view_literals;

namespace m
{
    namespace tracing
    {
        class message;
        class message_queue;

        class imessage
        {
        public:
            using value_type = wchar_t;

            imessage()                = default;
            virtual ~imessage()       = default;
            imessage(imessage const&) = delete;
            imessage(imessage&&)      = delete;

            // the clear() and push_back() member functions are provided
            // for the caller to implement std::format_to() functionality.
            virtual void
            clear() = 0;

            virtual void
            push_back(wchar_t const& wch) = 0;

            virtual std::wstring_view
            view() = 0;

            virtual event_kind
            kind() const = 0;

            virtual void
            kind(event_kind kind) = 0;

            virtual tracing::event_context const*
            event_context() const = 0;

            virtual void
            event_context(tracing::event_context const& ec) = 0;

            virtual void
            copy_into(m::not_null<imessage*> msg) = 0;

            virtual tracing::gdsn
            unique_id() = 0;
        };

    } // namespace tracing
} // namespace m
