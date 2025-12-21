// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include <Windows.h>

#include <m/threadpool/threadpool.h>
#include <m/utility/pointers.h>
#include <m/win32/filetime_clock.h>
#include <m/win32/threadpool.h>

#include "threadpool.h"

namespace m::threadpool_impl
{
    class periodic_timer : public m::periodic_timer
    {
    public:
        periodic_timer() = delete;
        periodic_timer(std::packaged_task<timer_callable>&& task, std::wstring description);

        periodic_timer(m::threadpool_impl::periodic_timer&& other) = delete;
        periodic_timer(m::threadpool_impl::periodic_timer const&)  = delete;
        ~periodic_timer();

        void
        operator=(m::threadpool_impl::periodic_timer const&) = delete;

        m::threadpool_impl::periodic_timer&
        operator=(m::threadpool_impl::periodic_timer&& other) = delete;

        void
        swap(periodic_timer&) = delete;

    protected:
        void
        do_set(m::duration dur) override;

        bool
        do_is_set() override;

        void
        do_stop() override;

        void
        do_wait() override;

        struct timer_parameters
        {
            FILETIME  m_buffer_do_not_pass; // Buffer only, do not pass as pftDueTime
            PFILETIME m_p_ft_due_time;
            DWORD     m_ms_period;
            DWORD     m_ms_window_length;
        };

        static void
        compute_timer_times(duration dur, timer_parameters& parameters);

        static void
        tp_timer_callback(PTP_CALLBACK_INSTANCE tp_callback_instance,
                          PVOID                 instance,
                          PTP_TIMER             periodic_timer);

        void on_tp_timer(PTP_CALLBACK_INSTANCE) noexcept;

        mutable std::mutex                 m_mutex;
        std::packaged_task<timer_callable> m_packaged_task;
        duration                           m_duration;
        std::wstring                       m_description;
        std::uintmax_t                     m_set_count{};
        std::uintmax_t                     m_set_count_when_cancelled{};
        std::uintmax_t                     m_set_count_when_executed{};
        std::uintmax_t                     m_set_count_when_finalized{};
        std::uintmax_t                     m_re_execution_count{};
        m::win32::threadpool::tp_timer     m_timer;
    };
} // namespace m::threadpool_impl
