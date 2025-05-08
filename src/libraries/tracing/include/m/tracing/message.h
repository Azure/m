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

#include <m/cast/try_cast.h>
#include <m/strings/literal_string_view.h>

#include "event_context.h"
#include "event_kind.h"
#include "safe_array_iterator.h"

using namespace m::string_view_literals;

namespace m
{
    namespace tracing
    {
        class message;
        class message_queue;

        class message
        {
        public:
            message()               = default;
            ~message()              = default;
            message(message const&) = delete;
            message(message&&)      = delete;
            void
            operator=(message const&);

            std::wstring_view
            view();

            template <typename FormatStringT, typename FormatArgsT>
            void
            format_log(FormatStringT&& fmt, FormatArgsT format_args)
            {
                auto       it    = safe_array_iterator(m_chars, 0);
                auto       endit = std::vformat_to(it, fmt.get(), format_args);
                auto const diff  = &*endit - &*it;
                m_length         = m::try_cast<std::size_t>(diff);
            }

            // private:
            std::size_t               m_length;
            std::array<wchar_t, 4096> m_chars;
            event_context             m_event_context;
        };

    } // namespace tracing
} // namespace m
