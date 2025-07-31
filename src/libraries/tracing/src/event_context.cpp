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
    event_context::event_context() noexcept: m_os_process_id{}, m_os_thread_id{}, m_time_point{} {}

    event_context::event_context(event_context const& other) noexcept:
        m_os_process_id(other.m_os_process_id),
        m_os_thread_id(other.m_os_thread_id),
        m_time_point(other.m_time_point)
    {}

    event_context&
    event_context::operator=(event_context const& other) noexcept
    {
        m_os_process_id = other.m_os_process_id;
        m_os_thread_id  = other.m_os_thread_id;
        m_time_point    = other.m_time_point;
        return *this;
    }

    event_context
    event_context::current()
    {
        event_context retval{};

#ifdef WIN32
        retval.m_os_process_id = ::GetCurrentProcessId();
        retval.m_os_thread_id  = ::GetCurrentThreadId();
#else
        retval.m_os_process_id = getpid();
        retval.m_os_thread_id  = gettid();
#endif

        retval.m_time_point = std::chrono::utc_clock::now();
        return retval;
    }
} // namespace m::tracing
