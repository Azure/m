// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <chrono>
#include <variant>

#include <m/debugging/dbg_format.h>

#include "threadpool_timer_impl.h"

using namespace std::chrono_literals;

m::threadpool_impl::timer::timer(m::threadpool_impl::timer::task_type&& task):
    m_task(std::move(task))
{
    m_timer = ::CreateThreadpoolTimer(tp_timer_callback, this, nullptr);
}

m::threadpool_impl::timer::~timer()
{
    if (auto timer = std::exchange(m_timer, nullptr); timer != nullptr)
    {
        ::SetThreadpoolTimer(timer, nullptr, 0, 0);
        ::WaitForThreadpoolTimerCallbacks(timer, TRUE);
        ::CloseThreadpoolTimer(timer);
    }
}

bool
m::threadpool_impl::timer::do_cancel_requested()
{
    auto l = std::unique_lock(m_mutex);
    return m_cancel_requested;
}

bool
m::threadpool_impl::timer::do_done()
{
    return m_done.load(std::memory_order_acquire);
}

void
m::threadpool_impl::timer::do_try_cancel()
{
    // Can't try to hold the mutex here since the mutex is held over the
    // task execution!
    m_cancel_requested.store(true, std::memory_order_release);
}

void
m::threadpool_impl::timer::do_set(duration dur)
{
    auto l = std::unique_lock(m_mutex);

    m_duration = dur;
    m_done.store(false, std::memory_order_release);
    m_task.reset();

    set_threadpool_timer_ex_parameters parameters;
    compute_timer_times(dur, parameters);

    bool cancelled = !!::SetThreadpoolTimerEx(
        m_timer, parameters.m_p_ft_due_time, parameters.m_ms_period, parameters.m_ms_window_length);
    std::ignore = cancelled;
}

void
m::threadpool_impl::timer::compute_timer_times(duration                            dur,
                                               set_threadpool_timer_ex_parameters& parameters)
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
        auto const filetime_duration =
            std::chrono::duration_cast<m::threadpool_impl::file_time>(dur);

        ULARGE_INTEGER uli_filetime{};
        uli_filetime.QuadPart = -filetime_duration.count();

        parameters.m_buffer_do_not_pass.dwLowDateTime  = uli_filetime.LowPart;
        parameters.m_buffer_do_not_pass.dwHighDateTime = uli_filetime.HighPart;
        parameters.m_p_ft_due_time                     = &parameters.m_buffer_do_not_pass;
    }

    parameters.m_ms_period = 0; // these are not periodic timers
    parameters.m_ms_window_length =
        100; // allow for 1/10th of a second of window for system to batch timers
}

void
m::threadpool_impl::timer::tp_timer_callback(PTP_CALLBACK_INSTANCE tp_callback_instance,
                                             PVOID                 instance,
                                             PTP_TIMER /* timer*/)
{
    auto const this_ptr = reinterpret_cast<timer*>(instance);
    this_ptr->on_tp_timer(tp_callback_instance);
}

void
m::threadpool_impl::timer::on_tp_timer(PTP_CALLBACK_INSTANCE) noexcept
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

    m_task(m_cancel_requested);

    m_done.store(true, std::memory_order_release);
}
