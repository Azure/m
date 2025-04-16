// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <iostream>
#include <memory>
#include <string_view>

#include <m/strings/literal_string_view.h>
#include <m/tracing/cout_sink.h>
#include <m/tracing/event_context.h>
#include <m/tracing/tracing.h>

#ifdef WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace m::tracing
{
    event_context::event_context(): m_process_id{}, m_thread_id{}, m_time_point{} {}

    event_context::event_context(event_context const& other):
        m_process_id(other.m_process_id),
        m_thread_id(other.m_thread_id),
        m_time_point(other.m_time_point)
    {}

    event_context&
    event_context::operator=(event_context const& other)
    {
        m_process_id = other.m_process_id;
        m_thread_id  = other.m_thread_id;
        m_time_point = other.m_time_point;
        return *this;
    }

    constexpr void
    swap(event_context& l, event_context& r) noexcept
    {
        using std::swap;

        swap(l.m_process_id, r.m_process_id);
        swap(l.m_thread_id, r.m_thread_id);
        swap(l.m_time_point, r.m_time_point);
    }

    event_context
    event_context::current()
    {
        event_context retval{};

#ifdef WIN32
        retval.m_process_id = ::GetCurrentProcessId();
#else
        retval.m_process_id = getpid();
#endif
        retval.m_thread_id  = std::this_thread::get_id();
        retval.m_time_point = std::chrono::utc_clock::now();
        return retval;
    }
} // namespace m::tracing