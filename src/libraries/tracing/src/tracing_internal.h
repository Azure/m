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
#include <memory>
#include <mutex>
#include <new>
#include <queue>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <m/debugging/dbg_format.h>
#include <m/strings/literal_string_view.h>
#include <m/tracing/channel.h>
#include <m/tracing/envelope.h>
#include <m/tracing/event_kind.h>
#include <m/tracing/format_view.h>
#include <m/tracing/message_processor.h>
#include <m/tracing/message_queue.h>
#include <m/tracing/message_source.h>
#include <m/tracing/multiplexor.h>
#include <m/tracing/on_message_disposition.h>
#include <m/tracing/sink.h>
#include <m/tracing/topology_version.h>
#include <m/utility/pointers.h>

using namespace std::string_view_literals;

#define M_TRACING_DEBUGGING 0

namespace m::tracing_impl
{
    constexpr bool debugging = M_TRACING_DEBUGGING ? true : false;

    template <typename... Types>
    void
    trace_dbg(std::wformat_string<Types...> fmt, Types&&... args)
    {
        if constexpr (debugging)
        {
            dbg_wprintln_v(fmt, std::forward<Types>(args)...);
        }
    }

#if M_TRACING_DEBUGGING
    struct tr_frame
    {
        template <typename ThisT>
        tr_frame(m::not_null<char const*> function_name, ThisT* ptr):
            m_function_name(function_name), m_thisptr(reinterpret_cast<std::uintptr_t>(ptr))
        {
            trace_dbg(L"Entered {:x} -> {}", m_thisptr.value(), m_function_name);
        }

        tr_frame(m::not_null<char const*> function_name):
            m_function_name(function_name), m_thisptr(std::nullopt)
        {
            trace_dbg(L"Entered {}", m_function_name);
        }

        ~tr_frame()
        {
            std::wstring_view success = L"WITH FAILURE"sv;

            if (m_succeeded)
                success = L""sv;

            if (m_thisptr.has_value())
            {
                trace_dbg(L"Exiting {}{:x} -> {}", success, m_thisptr.value(), m_function_name);
            }
            else
            {
                trace_dbg(L"Exiting {}{}", success, m_function_name);
            }
        }

        template <typename... Types>
        void
        write(const std::wformat_string<Types...> fmt, Types&&... args)
        {
            if (m_thisptr.has_value())
            {
                dbg_wprint(L"{:x}->{}: ", m_thisptr.value(), m_function_name);
            }
            else
            {
                dbg_wprint(L"{}: ", m_function_name);
            }

            trace_dbg(fmt, std::forward<Types>(args)...);
        }

        void
        succeeded()
        {
            m_succeeded = true;
        }

        template <typename T>
        decltype(auto)
        succeeded(T&& v)
        {
            m_succeeded = true;
            return std::forward<T>(v);
        }

    private:
        m::tracing::format_view<char> m_function_name;
        std::optional<std::uintptr_t> m_thisptr;
        bool                          m_succeeded{false};
    };
#else
    struct tr_frame
    {
        template <typename ThisT>
        tr_frame(m::not_null<char const*>, ThisT*)
        {}

        tr_frame(m::not_null<char const*>) {}

        void
        succeeded()
        {}

        template <typename... Types>
        void
        write(const std::wformat_string<Types...>, Types&&...)
        {}

        template <typename T>
        decltype(auto)
        succeeded(T&& v)
        {
            return std::forward<T>(v);
        }
    };
#endif

} // namespace m::tracing_impl
