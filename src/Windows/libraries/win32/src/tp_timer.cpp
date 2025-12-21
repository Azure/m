// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <tuple>

#include <m/cast/to.h>
#include <m/error_handling/macros.h>
#include <m/errors/errors.h>
#include <m/win32/threadpool.h>
#include <m/windows_chrono/windows_chrono_casts.h>

namespace m::win32::threadpool
{
    tp_timer::tp_timer(PTP_TIMER_CALLBACK pfnwait, PVOID pv, PTP_CALLBACK_ENVIRON pcbe)
    {
        m_ptp_timer = ::CreateThreadpoolTimer(pfnwait, pv, pcbe);
        if (m_ptp_timer == PTP_TIMER{})
            m::throw_last_win32_error();
    }

    tp_timer::~tp_timer() { reset(); }

    void
    tp_timer::reset(PTP_TIMER ptp_timer_new) noexcept
    {
        auto const ptp_timer_old = std::exchange(m_ptp_timer, ptp_timer_new);
        if (ptp_timer_old != nullptr)
        {
            ::CloseThreadpoolTimer(ptp_timer_old);
        }
    }

    bool
    tp_timer::is_set()
    {
        M_INTERNAL_ERROR_CHECK(m_ptp_timer != PTP_TIMER{});
        return ::IsThreadpoolTimerSet(m_ptp_timer);
    }

    void
    tp_timer::set(PFILETIME pftDueTime, DWORD msPeriod, DWORD msWindowLength)
    {
        M_INTERNAL_ERROR_CHECK(m_ptp_timer != PTP_TIMER{});
        ::SetThreadpoolTimerEx(m_ptp_timer, pftDueTime, msPeriod, msWindowLength);
    }

    void
    tp_timer::set(FILETIME                                 due_time,
                  std::optional<std::chrono::milliseconds> period,
                  std::optional<std::chrono::milliseconds> window_length)
    {
        DWORD ms_period{};
        DWORD ms_window_length{};

        if (period.has_value())
            ms_period = m::to<DWORD>(period.value().count());

        if (window_length.has_value())
            ms_window_length = m::to<DWORD>(window_length.value().count());

        set(&due_time, ms_period, ms_window_length);
    }

    void
    tp_timer::cancel()
    {
        set(nullptr, 0, 0);
    }

    void
    tp_timer::wait_for_callbacks(bool cancel_pending_callbacks)
    {
        M_INTERNAL_ERROR_CHECK(m_ptp_timer != PTP_TIMER{});
        ::WaitForThreadpoolTimerCallbacks(m_ptp_timer, cancel_pending_callbacks);
    }

} // namespace m::win32::threadpool
