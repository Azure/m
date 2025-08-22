// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <tuple>

#include <m/error_handling/macros.h>
#include <m/errors/errors.h>
#include <m/win32/threadpool.h>
#include <m/windows_chrono/windows_chrono_casts.h>

namespace m::win32::threadpool
{
    tp_wait::tp_wait(tp_wait&& other) noexcept: m_pwait{PTP_WAIT{}}
    {
        using std::swap;
        swap(m_pwait, other.m_pwait);
    }

    tp_wait::tp_wait(PTP_WAIT_CALLBACK pfnwait, PVOID pv, PTP_CALLBACK_ENVIRON pcbe)
    {
        m_pwait = ::CreateThreadpoolWait(pfnwait, pv, pcbe);
        if (m_pwait == PTP_WAIT{})
            m::throw_last_win32_error();
    }

    tp_wait::~tp_wait() { reset(); }

    tp_wait&
    tp_wait::operator=(tp_wait&& other) noexcept
    {
        using std::swap;
        swap(m_pwait, other.m_pwait);
        return *this;
    }

    void
    tp_wait::reset()
    {
        if (auto const pwait = std::exchange(m_pwait, PTP_WAIT{}); pwait != PTP_WAIT{})
        {
            ::SetThreadpoolWaitEx(pwait, nullptr, nullptr, nullptr);
            ::WaitForThreadpoolWaitCallbacks(pwait, TRUE);
            ::CloseThreadpoolWait(pwait);
        }
    }

    void
    tp_wait::wait_for_callbacks(bool cancel_pending_callbacks)
    {
        M_INTERNAL_ERROR_CHECK(m_pwait != PTP_WAIT{});
        ::WaitForThreadpoolWaitCallbacks(m_pwait, cancel_pending_callbacks);
    }

    void
    tp_wait::set_wait(HANDLE h)
    {
        ::SetThreadpoolWaitEx(m_pwait, h, nullptr, nullptr);
    }

    void
    tp_wait::do_set_wait_for(HANDLE h, std::chrono::milliseconds ms)
    {
        auto ft = m::to<FILETIME>(ms);
        ::SetThreadpoolWaitEx(m_pwait, h, &ft, nullptr);
    }

    void
    tp_wait::do_set_wait_until(HANDLE h, utc_time_point tp)
    {
        auto ft = m::to<FILETIME>(tp);
        ::SetThreadpoolWaitEx(m_pwait, h, &ft, nullptr);
    }

} // namespace m::win32::threadpool
