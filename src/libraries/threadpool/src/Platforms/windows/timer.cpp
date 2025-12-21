// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <chrono>
#include <variant>

#include <m/debugging/dbg_format.h>
#include <m/formatters/FILETIME.h>
#include <m/thread_description/thread_description.h>
#include <m/tracing/format_view.h>
#include <m/tracing/frame.h>
#include <m/tracing/tracing.h>
#include <m/utility/mutex.h>

#include "timer.h"

using namespace std::chrono_literals;

namespace
{
    /// <summary>
    /// This is the "window length" for timers that we're using.
    ///
    /// This gives the windows scheduler this amount of time to keep cores
    /// powered down to save electricity rather than powering them up to
    /// force scheduling of work items.
    ///
    /// I have no idea if this is a good value or if this needs to be
    /// configurable or what. This is just an "initial ante".
    /// </summary>
    constexpr DWORD ms_window_length = 100;

} // namespace

namespace m::threadpool_impl
{
    timer::timer(std::packaged_task<timer_callable>&& task, std::wstring description):
        m_packaged_task(std::move(task)),
        m_description(std::move(description)),
        m_timer(tp_timer_callback, this),
        m_duration{}
    {
        wtrace_verbose(L"{} constructed timer {:x}",
                       tracing::format_view(__FUNCTION__),
                       reinterpret_cast<uintptr_t>(this));
    }

    timer::~timer()
    {
        wtrace_verbose(L"{} destroying timer {:x}",
                       tracing::format_view(__FUNCTION__),
                       reinterpret_cast<uintptr_t>(this));

        m_timer.cancel();
        m_timer.wait_for_callbacks(true);
    }

    void
    timer::do_set(duration dur)
    {
        tracing::frame frame(__FUNCTION__, this);

        timer_parameters params;

        compute_timer_times(dur, params);

        auto l           = std::unique_lock(m_mutex);
        m_cancel_on_wait = false;
        m_duration       = dur;
        m_version++;
        m_timer.set(params.m_p_ft_due_time, params.m_ms_period, params.m_ms_window_length);
        frame.succeeded();
    }

    void
    timer::compute_timer_times(duration dur, timer_parameters& params)
    {
        params.m_buffer_do_not_pass.dwLowDateTime  = 0;
        params.m_buffer_do_not_pass.dwHighDateTime = 0;
        params.m_p_ft_due_time                     = nullptr;
        params.m_ms_period                         = 0;
        params.m_ms_window_length                  = 0;

        if (dur.count() == 0)
        {
            // Yes these were just set in the initialization of the
            // function but it's important that these be zero
            // semantically and the compiler should optimize these
            // down to one set of moves.
            params.m_buffer_do_not_pass.dwLowDateTime  = 0;
            params.m_buffer_do_not_pass.dwHighDateTime = 0;
            params.m_p_ft_due_time                     = &params.m_buffer_do_not_pass;
        }
        else
        {
            auto const filetime_duration = std::chrono::duration_cast<file_time>(dur);

            ULARGE_INTEGER uli_filetime{};
            uli_filetime.QuadPart = -filetime_duration.count();

            params.m_buffer_do_not_pass.dwLowDateTime  = uli_filetime.LowPart;
            params.m_buffer_do_not_pass.dwHighDateTime = uli_filetime.HighPart;
            params.m_p_ft_due_time                     = &params.m_buffer_do_not_pass;
        }

        params.m_ms_period        = 0; // these are not periodic timers
        params.m_ms_window_length = ms_window_length;
    }

    bool
    timer::do_is_set()
    {
        tracing::frame frame(__FUNCTION__, this);
        return frame.succeededv(m_timer.is_set());
    }

    void
    timer::do_wait()
    {
        tracing::frame frame(__FUNCTION__, this);
        auto const     cancel_on_wait =
            with_unique_lock(m_mutex, [this]() { return m_cancel_on_wait; });

        m_timer.wait_for_callbacks(cancel_on_wait);

        with_unique_lock(m_mutex, [this]() { m_cancel_on_wait = false; });

        frame.succeeded();
    }

    void
    timer::do_cancel()
    {
        tracing::frame frame(__FUNCTION__, this);
        m_timer.cancel();
        with_unique_lock(m_mutex, [this]() { m_cancel_on_wait = true; });
        frame.succeeded();
    }

    void
    timer::tp_timer_callback(PTP_CALLBACK_INSTANCE tp_callback_instance, PVOID instance, PTP_TIMER)
    {
        tracing::frame frame(__FUNCTION__);
        auto const     this_ptr = reinterpret_cast<timer*>(instance);
        this_ptr->on_tp_timer(tp_callback_instance);
        frame.succeeded();
    }

    void
    timer::on_tp_timer(PTP_CALLBACK_INSTANCE) noexcept
    {
        tracing::frame        frame(__FUNCTION__, this);
        m::thread_description td;
        decltype(m_version)   version{};

        auto const packaged_task = with_unique_lock(m_mutex, [&]() {
            auto return_value = &m_packaged_task;

            if (m_in_dispatch)
            {
                return_value = nullptr;
            }
            else
            {
                td.set(m_description);
                version               = m_version;
                m_version_at_dispatch = m_version;
                m_packaged_task.reset();
                m_in_dispatch = true;
            }
            return return_value;
        });

        if (packaged_task == nullptr)
        {
            // If we get nullptr here it's because we're already in
            // the process of handling another callback so we will
            // just drop this one. Otherwise we perhaps have to queue
            // them, or deal with the deadlocks associated with the
            // waits on finalization a different way.
            return;
        }

        (*packaged_task)();

        with_unique_lock(m_mutex, [this, version]() {
            m_version_done = version;
            m_in_dispatch  = false;
        });
        frame.succeeded();
    }
} // namespace m::threadpool_impl
