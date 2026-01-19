// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <chrono>
#include <variant>

#include <m/debugging/dbg_format.h>
#include <m/formatters/FILETIME.h>
#include <m/thread_description/thread_description.h>
#include <m/tracing/tracing.h>

#include "periodic_timer.h"

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
    periodic_timer::periodic_timer(std::packaged_task<timer_callable>&& task,
                                   std::wstring                         description):
        m_packaged_task(std::move(task)),
        m_description(std::move(description)),
        m_timer(tp_timer_callback, this)
    {}

    periodic_timer::~periodic_timer()
    {
        m_timer.cancel();
        m_timer.wait_for_callbacks(true);
    }

    void
    periodic_timer::do_set(duration_type const& dur)
    {
        timer_parameters parameters;

        compute_timer_times(dur, parameters);

        auto l = std::unique_lock(m_mutex);

        m_duration = dur;
        m_packaged_task.reset();
        m_timer.set(
            parameters.m_p_ft_due_time, parameters.m_ms_period, parameters.m_ms_window_length);
    }

    bool
    periodic_timer::do_is_set()
    {
        return m_timer.is_set();
    }

    void
    periodic_timer::do_stop()
    {
        auto l = std::unique_lock(m_mutex);

        // You shouldn't stop a periodic task you haven't started.
        if ((m_set_count == m_set_count_when_finalized) ||
            (m_set_count == m_set_count_when_cancelled))
        {
            m::wtrace_error(L"Attempted to stop a periodic task that was not started");
            throw m::precondition_not_met(
                "Periodic task tried to be stopped when it was not running");
        }

        m_timer.cancel();
    }

    void
    periodic_timer::compute_timer_times(duration_type dur, timer_parameters& parameters)
    {
        // Higher layer should have performed a proper parameter check.
        // we don't want to have to deal with edge cases here.
        M_INTERNAL_ERROR_CHECK(dur.count() > 0);

        parameters.m_buffer_do_not_pass.dwLowDateTime  = 0;
        parameters.m_buffer_do_not_pass.dwHighDateTime = 0;
        parameters.m_p_ft_due_time                     = nullptr;
        parameters.m_ms_period                         = 0;
        parameters.m_ms_window_length                  = 0;

        auto const filetime_duration = std::chrono::duration_cast<m::win32::filetime_duration>(dur);

        ULARGE_INTEGER uli_filetime{};
        uli_filetime.QuadPart = -filetime_duration.count();

        // Handle a case where the value was rounded to zero somehow
        if (uli_filetime.QuadPart == 0)
            uli_filetime.QuadPart = 0xffffffffffffffffull;

        // Should be obvious but .. you know, 2s complement math
        M_INTERNAL_ERROR_CHECK(uli_filetime.QuadPart >= 0x80000000'00000000ull);

        parameters.m_buffer_do_not_pass.dwLowDateTime  = uli_filetime.LowPart;
        parameters.m_buffer_do_not_pass.dwHighDateTime = uli_filetime.HighPart;
        parameters.m_p_ft_due_time                     = &parameters.m_buffer_do_not_pass;

        using T                 = decltype(parameters.m_ms_period);
        constexpr auto maxvalue = (std::numeric_limits<T>::max)();

        // Get the duration, into milliseconds, with whatever numeric range is lossless
        auto v = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();

        if (v > maxvalue)
            parameters.m_ms_period = maxvalue;
        else
            parameters.m_ms_period = static_cast<T>(v);

        parameters.m_ms_window_length = ms_window_length;
    }

    void
    periodic_timer::do_wait()
    {
        m_timer.wait_for_callbacks(false);
    }

    void
    periodic_timer::tp_timer_callback(PTP_CALLBACK_INSTANCE tp_callback_instance,
                                      PVOID                 instance,
                                      PTP_TIMER /* periodic_timer*/)
    {
        auto const this_ptr = reinterpret_cast<periodic_timer*>(instance);
        this_ptr->on_tp_timer(tp_callback_instance);
    }

    void
    periodic_timer::on_tp_timer(PTP_CALLBACK_INSTANCE) noexcept
    {
        auto l = std::unique_lock(m_mutex);

        m_set_count_when_executed = m_set_count;

        {
            m::thread_description td(m_description);
            m_packaged_task();
        }

        m_packaged_task.reset();
        m_re_execution_count++;
    }
} // namespace m::threadpool_impl
