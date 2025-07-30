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
#include <m/strings/literal_string_view.h>
#include <m/tracing/channel.h>
#include <m/tracing/event_kind.h>
#include <m/tracing/message.h>
#include <m/tracing/message_queue.h>
#include <m/tracing/multiplexor.h>
#include <m/tracing/on_message_disposition.h>
#include <m/tracing/safe_array_iterator.h>
#include <m/tracing/sink.h>
#include <m/utility/pointers.h>

using namespace m::string_view_literals;

namespace m
{
    namespace tracing
    {
        class channel;
        class monitor_class;

        class source
        {
        public:
            source(m::not_null<monitor_class*>              monitor,
                   event_kind                               kind,
                   std::initializer_list<std::wstring_view> channels);

            source(m::not_null<monitor_class*> monitor, event_kind kind, std::wstring_view channel);

            template <typename InputIt>
            source(m::not_null<monitor_class*> monitor,
                   event_kind                  kind,
                   InputIt                     channels_begin,
                   InputIt                     channels_end):
                m_monitor{monitor}, m_channels(channels_begin, channels_end), m_event_kind{kind}
            {}

            template <typename... Types>
            void
            wlog(event_kind kind, std::wformat_string<Types...> fmt, Types&&... args);

            template <typename... Types>
            void
            wlog(std::wformat_string<Types...> fmt, Types&&... args);

            template <typename... Types>
            void
            log(event_kind kind, std::format_string<Types...> fmt, Types&&... args);

            template <typename... Types>
            void
            log(std::format_string<Types...> fmt, Types&&... args);

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
            internal_log(event_kind kind, FormatStringT&& fmt, FormatArgsT&& format_args)
            {
                if (!m_closed && do_test_kind(kind))
                {
                    auto qitem = m_multiplexor->reserve_message(kind);
                    qitem.get_message()->format_log(fmt, std::forward<FormatArgsT>(format_args));
                    qitem.get_message()->m_event_context = event_context::current();
                    std::ignore                          = m_multiplexor->on_message(qitem);
                }
            }

            m::not_null<monitor_class*>        m_monitor;
            std::shared_ptr<multiplexor>       m_multiplexor;
            std::vector<std::wstring>          m_channel_names;
            std::vector<m::not_null<channel*>> m_channels;
            event_kind                         m_event_kind;
            bool                               m_closed{false};
        };

        template <typename... Types>
        void
        source::wlog(event_kind kind, std::wformat_string<Types...> fmt, Types&&... args)
        {
            internal_log(kind, std::forward<decltype(fmt)>(fmt), std::make_wformat_args(args...));
        }

        template <typename... Types>
        void
        source::wlog(std::wformat_string<Types...> fmt, Types&&... args)
        {
            internal_log(event_kind::information,
                         std::forward<decltype(fmt)>(fmt),
                         std::make_wformat_args(args...));
        }

        template <typename... Types>
        void
        source::log(event_kind kind, std::format_string<Types...> fmt, Types&&... args)
        {
            internal_log(kind, std::forward<decltype(fmt)>(fmt), std::make_format_args(args...));
        }

        template <typename... Types>
        void
        source::log(std::format_string<Types...> fmt, Types&&... args)
        {
            internal_log(event_kind::information,
                         std::forward<decltype(fmt)>(fmt),
                         std::make_format_args(args...));
        }
    } // namespace tracing
} // namespace m
