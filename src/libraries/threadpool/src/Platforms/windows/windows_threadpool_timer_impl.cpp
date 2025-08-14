// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <chrono>
#include <variant>

#include <m/debugging/dbg_format.h>
#include <m/formatters/FILETIME.h>
#include <m/thread_description/thread_description.h>
#include <m/tracing/tracing.h>

#include "windows_threadpool_timer_impl.h"

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

namespace m::threadpool_impl::windows
{
    timer::timer(normal_timer_tag_t,
                 std::packaged_task<timer_normal_callable>&& task,
                 std::wstring                                description):
        m::threadpool_impl::timer(normal_timer_tag, std::move(task), std::move(description))
    {
        m_timer = ::CreateThreadpoolTimer(tp_timer_callback, this, nullptr);
    }

    timer::timer(cancellable_timer_tag_t,
                 std::packaged_task<timer_cancellable_callable>&& task,
                 std::wstring                                     description):
        m::threadpool_impl::timer(cancellable_timer_tag, std::move(task), std::move(description))
    {
        m_timer = ::CreateThreadpoolTimer(tp_timer_callback, this, nullptr);
    }

    timer::timer(periodic_timer_tag_t,
                 std::packaged_task<timer_normal_callable>&& task,
                 std::wstring                                description):
        m::threadpool_impl::timer(periodic_timer_tag, std::move(task), std::move(description))
    {
        m_timer = ::CreateThreadpoolTimer(tp_timer_callback, this, nullptr);
    }

    timer::~timer()
    {
        if (auto timer = std::exchange(m_timer, nullptr); timer != nullptr)
        {
            ::SetThreadpoolTimer(timer, nullptr, 0, 0);
            ::WaitForThreadpoolTimerCallbacks(timer, TRUE);
            ::CloseThreadpoolTimer(timer);
        }
    }

    void
    timer::do_set(duration dur)
    {
        set_threadpool_timer_ex_parameters parameters;

        if (m_task_type == task_type::periodic)
            compute_periodic_timer_times(dur, parameters);
        else
            compute_normal_timer_times(dur, parameters);

        auto l = std::unique_lock(m_mutex);

        m_duration = dur;
        m_done.store(false, std::memory_order_release);

        switch (m_task_type)
        {
            using enum task_type;

            default: M_UNREACHABLE_CODE();

            case normal: base_type::m_normal_packaged_task.reset(); break;
            case periodic: base_type::m_normal_packaged_task.reset(); break;
            case cancellable: base_type::m_cancellable_packaged_task.reset(); break;
        }

        bool cancelled = !!::SetThreadpoolTimerEx(m_timer,
                                                  parameters.m_p_ft_due_time,
                                                  parameters.m_ms_period,
                                                  parameters.m_ms_window_length);
        std::ignore    = cancelled;
    }

    void
    timer::do_stop()
    {
        auto l = std::unique_lock(m_mutex);

        // should only be reached from periodic timers
        M_INTERNAL_ERROR_CHECK(m_task_type == task_type::periodic);

        // You shouldn't stop a periodic task you haven't started.
        if (!m_started)
        {
            m::wtrace_error(L"Attempted to stop a periodic task that was not started");
            throw m::precondition_not_met(
                "Periodic task tried to be stopped when it was not running");
        }

        bool cancelled = !!::SetThreadpoolTimerEx(m_timer, nullptr, 0, 0);

        // If the return says it wasn't cancelled, is it because it's still
        // running, or because it was already cancelled?
        //
        // In the first case, this is fatal. If we were not able to cancel
        // the task when we were supposed to be able to, this is disastrous.
        //
        // On the other hand, if the task was not actually set and this was
        // an errant call to stop(), this is only a programmer error.
        //
        // Aha! inconsistent state => shut down the process before we
        // violate security
        //
        // detected a major programmer error => shut down the process before
        // we violate security.
        //
        // In either case we have a significant contract violation.
        //
        M_INTERNAL_ERROR_CHECK(cancelled);
    }

    void
    timer::compute_normal_timer_times(duration dur, set_threadpool_timer_ex_parameters& parameters)
    {
        parameters.m_buffer_do_not_pass.dwLowDateTime  = 0;
        parameters.m_buffer_do_not_pass.dwHighDateTime = 0;
        parameters.m_p_ft_due_time                     = nullptr;
        parameters.m_ms_period                         = 0;
        parameters.m_ms_window_length                  = 0;

        if (dur.count() == 0)
        {
            // Yes these were just set in the initialization of the
            // function but it's important that these be zero
            // semantically and the compiler should optimize these
            // down to one set of moves.
            parameters.m_buffer_do_not_pass.dwLowDateTime  = 0;
            parameters.m_buffer_do_not_pass.dwHighDateTime = 0;
            parameters.m_p_ft_due_time                     = &parameters.m_buffer_do_not_pass;
        }
        else
        {
            auto const filetime_duration = std::chrono::duration_cast<file_time>(dur);

            ULARGE_INTEGER uli_filetime{};
            uli_filetime.QuadPart = -filetime_duration.count();

            parameters.m_buffer_do_not_pass.dwLowDateTime  = uli_filetime.LowPart;
            parameters.m_buffer_do_not_pass.dwHighDateTime = uli_filetime.HighPart;
            parameters.m_p_ft_due_time                     = &parameters.m_buffer_do_not_pass;
        }

        parameters.m_ms_period        = 0; // these are not periodic timers
        parameters.m_ms_window_length = ms_window_length;
    }

    void
    timer::compute_periodic_timer_times(duration                            dur,
                                        set_threadpool_timer_ex_parameters& parameters)
    {
        // Higher layer should have performed a proper parameter check.
        // we don't want to have to deal with edge cases here.
        M_INTERNAL_ERROR_CHECK(dur.count() > 0);

        parameters.m_buffer_do_not_pass.dwLowDateTime  = 0;
        parameters.m_buffer_do_not_pass.dwHighDateTime = 0;
        parameters.m_p_ft_due_time                     = nullptr;
        parameters.m_ms_period                         = 0;
        parameters.m_ms_window_length                  = 0;

        auto const filetime_duration = std::chrono::duration_cast<file_time>(dur);

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
    timer::tp_timer_callback(PTP_CALLBACK_INSTANCE tp_callback_instance,
                             PVOID                 instance,
                             PTP_TIMER /* timer*/)
    {
        auto const this_ptr = reinterpret_cast<timer*>(instance);
        this_ptr->on_tp_timer(tp_callback_instance);
    }

    void
    timer::execute_task() noexcept
    {
        switch (m_task_type)
        {
            using enum task_type;

            default: M_UNREACHABLE_CODE();
            case periodic: m_normal_packaged_task(); break;
            case normal: m_normal_packaged_task(); break;
            case cancellable: m_cancellable_packaged_task(m_cancel_requested); break;
        }
    }

    void
    timer::on_tp_timer(PTP_CALLBACK_INSTANCE) noexcept
    {
        auto l = std::unique_lock(m_mutex);

        if (m_cancel_requested)
        {
            // If the cancellation request came in before we've started the task
            // we'll not even start it. Note that the state in this case is odd.
            // m_done == true, but m_started == false.
            m_done.store(true, std::memory_order_release);
            m_cancelled = true;
            return;
        }

        m_started = true;

        m::thread_description td(m_description);
        execute_task();

        //
        // This is a weird thing to state as a dichotomy but they are just two
        // independent actions, one of which happens for periodic tasks, one
        // for non-periodic tasks.
        //
        // Periodic tasks, since they will run again and even after some
        // theoretical rearchitecture, cannot return values, so their
        // std::packaged_task<> instances need to be reset. They are also
        // never "done".
        //
        // Normal and cancellable tasks are "done" once they have run, so
        // the "done" flag must be set.
        //
        // These two things are really unrelated but it seemed weird to have
        // if (condition) { ... } if (!condition) { ... }
        //
        if (m_task_type == task_type::periodic)
            m_normal_packaged_task.reset();
        else
            m_done.store(true, std::memory_order_release);
    }
} // namespace m::threadpool_impl::windows
