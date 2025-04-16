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

#include <m/cast/try_cast.h>

#include "channel.h"
#include "event_kind.h"
#include "message.h"
#include "message_queue.h"
#include "multiplexor.h"
#include "on_message_disposition.h"
#include "safe_array_iterator.h"
#include "sink.h"

namespace m
{
    namespace tracing
    {
        class channel;
        class monitor_class;
        class multiplexor;

        class source
        {
        public:
            source(m::not_null<monitor_class*>            monitor,
                   event_kind                               kind,
                   std::initializer_list<std::wstring_view> channels);

            source(m::not_null<monitor_class*> monitor,
                   event_kind                    kind,
                   std::wstring_view             channel);

            template <typename InputIt>
            source(m::not_null<monitor_class*> monitor,
                   event_kind                    kind,
                   InputIt                       channels_begin,
                   InputIt                       channels_end):
                m_monitor(monitor), m_event_kind(kind), m_channels(channels_begin, channels_end)
            {}

            template <typename... Types>
            void
            log(event_kind kind, std::wformat_string<Types...> fmt, Types const&... args);

            template <typename... Types>
            void
            log(std::wformat_string<Types...> fmt, Types const&... args);

            // Shut down this source - disconnect from the sink.
            void
            close();

            bool
            is_closed() const;

        protected:
            // Test whether event_kind is enabled for this source
            bool
            do_test_kind(event_kind kind);

            void
            do_close();

            template <typename FormatStringT, typename FormatArgsT>
            void
            internal_log(event_kind kind, FormatStringT&& fmt, FormatArgsT format_args)
            {
                if (!m_closed && do_test_kind(kind))
                {
                    auto qitem = m_multiplexor->reserve_message();
                    qitem.get_message()->format_log(fmt, format_args);
                    qitem.get_message()->m_event_context = event_context::current();
                    std::ignore                          = m_multiplexor->on_message(qitem);
                }
            }

            m::not_null<monitor_class*>        m_monitor;
            std::shared_ptr<multiplexor>         m_multiplexor;
            std::vector<std::wstring>            m_channel_names;
            std::vector<m::not_null<channel*>> m_channels;
            event_kind                           m_event_kind;
            bool                                 m_closed{false};
        };

        template <typename... Types>
        void
        source::log(event_kind kind, std::wformat_string<Types...> fmt, Types const&... args)
        {
            internal_log(kind, std::forward<decltype(fmt)>(fmt), std::make_wformat_args(args...));
#if 0
            if (!m_closed && do_test_kind(kind))
            {
                auto qitem = m_multiplexor->reserve_message();
                try
                {
                    using array_t   = decltype(qitem.m_message->m_chars);
                    auto format_args = std::make_wformat_args(args...);
                    auto it         = safe_array_iterator(qitem.m_message->m_chars, 0);
                    auto endit      = std::vformat_to(it, fmt.get(), format_args);
                    // auto const diff           = std::difference(it, endit);
                    auto const diff           = &*endit - &*it;
                    qitem.m_message->m_length = m::try_cast<std::size_t>(diff);
                    std::ignore               = m_multiplexor->on_message(qitem);
                }
                catch (...)
                {
                    // needs RAII support
                    qitem.m_return_queue->enqueue(qitem.m_message);
                    throw;
                }
            }
#endif
        }

        template <typename... Types>
        void
        source::log(std::wformat_string<Types...> fmt, Types const&... args)
        {
            internal_log(event_kind::information,
                         std::forward<decltype(fmt)>(fmt),
                         std::make_wformat_args(args...));
        }

    } // namespace tracing
} // namespace m
