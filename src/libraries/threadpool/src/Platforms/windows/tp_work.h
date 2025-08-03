// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

#include <m/errors/errors.h>

namespace m::windows_threadpool_impl
{
    class tp_work
    {
    public:
        tp_work() = default;
        tp_work(tp_work&& other) noexcept;
        tp_work(tp_work const& other) = delete;

        tp_work(PTP_WORK_CALLBACK pfnwk, PVOID pv, PTP_CALLBACK_ENVIRON pcbe);

        ~tp_work();

        tp_work&
        operator=(tp_work&&) noexcept;

        tp_work&
        operator=(tp_work const&) = delete;

        constexpr
        operator PTP_WORK() const noexcept
        {
            return m_pwk;
        }

        constexpr explicit
        operator bool() const noexcept
        {
            return m_pwk != nullptr;
        }

    private:
        PTP_WORK m_pwk{};
    };
} // namespace m::threadpool_impl
