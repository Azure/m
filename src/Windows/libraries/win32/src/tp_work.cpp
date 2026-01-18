// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <tuple>

#include <m/error_handling/macros.h>
#include <m/errors/errors.h>
#include <m/win32/threadpool.h>

namespace m::win32::threadpool
{
    tp_work::tp_work(tp_work&& other) noexcept: m_pwork{PTP_WORK{}}
    {
        using std::swap;
        swap(m_pwork, other.m_pwork);
    }

    tp_work::tp_work(PTP_WORK_CALLBACK pfnwk, PVOID pv, PTP_CALLBACK_ENVIRON pcbe)
    {
        m_pwork = ::CreateThreadpoolWork(pfnwk, pv, pcbe);
        if (m_pwork == PTP_WORK{})
            m::throw_last_win32_error();
    }

    tp_work::~tp_work() { reset(); }

    tp_work&
    tp_work::operator=(tp_work&& other) noexcept
    {
        using std::swap;
        swap(m_pwork, other.m_pwork);
        return *this;
    }

    void
    tp_work::reset()
    {
        if (auto const pwk = std::exchange(m_pwork, PTP_WORK{}); pwk != PTP_WORK{})
        {
            ::CloseThreadpoolWork(pwk);
        }
    }

    void
    tp_work::submit()
    {
        M_INTERNAL_ERROR_CHECK(m_pwork != PTP_WORK{});

        ::SubmitThreadpoolWork(m_pwork);
    }

    void
    tp_work::wait_for_callbacks(bool cancel_pending_callbacks)
    {
        M_INTERNAL_ERROR_CHECK(m_pwork != PTP_WORK{});
        ::WaitForThreadpoolWorkCallbacks(m_pwork, cancel_pending_callbacks);
    }

} // namespace m::win32::threadpool
