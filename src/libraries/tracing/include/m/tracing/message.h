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
#include <m/string_buffer/string_buffer.h>
#include <m/strings/literal_string_view.h>
#include <m/tracing/event_context.h>
#include <m/tracing/event_kind.h>
#include <m/tracing/safe_array_iterator.h>

using namespace m::string_view_literals;

namespace m
{
    namespace tracing
    {
        class message;
        class message_queue;

        class message : public imessage
        {
        public:
            message(std::shared_ptr<wpooled_string_buffer::pool_type> const& pool): m_buffer(pool)
            {}
            ~message()              = default;
            message(message const&) = delete;
            message(message&&)      = delete;

            std::wstring_view
            view() override;

            event_kind
            kind() const override;

            void
            kind(event_kind kind) override;

            void
            clear() override;

            void
            push_back(wchar_t const& wch) override;

            void
            event_context(tracing::event_context const&) override;

            tracing::event_context const*
            event_context() const override;

            void
            copy_into(m::not_null<imessage*> msg) override;

            template <typename FormatStringT, typename FormatArgsT>
            void
            vformat(FormatStringT&& fmt, FormatArgsT&& format_args)
            {
                m_buffer.clear();
                auto it = std::back_inserter(m_buffer);
                std::vformat_to(it, fmt.get(), std::forward<FormatArgsT>(format_args));
            }

            // private:
            event_kind                m_event_kind;
            m::wpooled_string_buffer  m_buffer;
            m::tracing::event_context m_event_context;
        };

    } // namespace tracing
} // namespace m
