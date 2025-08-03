// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <tuple>

#include <m/threadpool/threadpool.h>
#include <m/threadpool/work_item_state.h>
#include <m/threadpool/work_queue_execution_policy.h>

#include "tp_work.h"

namespace m::windows_threadpool_impl
{
    tp_work::tp_work(tp_work&& other) noexcept: m_pwk{}
    {
        using std::swap;
        swap(m_pwk, other.m_pwk);
    }

    tp_work::tp_work(PTP_WORK_CALLBACK pfnwk, PVOID pv, PTP_CALLBACK_ENVIRON pcbe = nullptr):
        m_pwk(::CreateThreadpoolWork(pfnwk, pv, pcbe))
    {
        if (m_pwk == nullptr)
            m::throw_last_win32_error();
    }

    tp_work::~tp_work()
    {
        if (auto pwk = std::exchange(m_pwk, nullptr); pwk != nullptr)
        {
            ::CloseThreadpoolWork(pwk);
        }
    }

    tp_work&
    tp_work::operator=(tp_work&& other) noexcept
    {
        using std::swap;
        swap(m_pwk, other.m_pwk);
        return *this;
    }

} // namespace m::threadpool_impl
