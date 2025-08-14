// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <utility>
#include <variant>
#include <vector>

#include <Windows.h>

#include <m/threadpool/threadpool.h>
#include <m/utility/pointers.h>

#include "../../threadpool_timer_impl.h"
#include "threadpool_impl.h"

namespace m::threadpool_impl::windows
{
    //
    // Windows FILETIME represents 10ns units
    //
    // using filetime_ratio = std::ratio<1, 100000000>;
    using filetime_ratio = std::ratio<1, 10000000>;

    using file_time = std::chrono::duration<long long, filetime_ratio>;

    class timer : public m::threadpool_impl::timer
    {
        using base_type = m::threadpool_impl::timer;

    public:
        timer(normal_timer_tag_t,
              std::packaged_task<timer_normal_callable>&& task,
              std::wstring                                description);

        timer(cancellable_timer_tag_t,
              std::packaged_task<timer_cancellable_callable>&& task,
              std::wstring                                     description);

        timer(periodic_timer_tag_t,
              std::packaged_task<timer_normal_callable>&& task,
              std::wstring                                description);

        timer(m::threadpool_impl::timer&& other) = delete;
        timer(m::threadpool_impl::timer const&)  = delete;
        ~timer();

        void
        operator=(m::threadpool_impl::timer const&) = delete;

        m::threadpool_impl::timer&
        operator=(m::threadpool_impl::timer&& other) = delete;

    protected:
        void
        do_set(duration dur) override;

        void
        do_stop() override;

        struct set_threadpool_timer_ex_parameters
        {
            FILETIME  m_buffer_do_not_pass; // Buffer only, do not pass as pftDueTime
            PFILETIME m_p_ft_due_time;
            DWORD     m_ms_period;
            DWORD     m_ms_window_length;
        };

        static void
        compute_normal_timer_times(duration dur, set_threadpool_timer_ex_parameters& parameters);

        static void
        compute_periodic_timer_times(duration dur, set_threadpool_timer_ex_parameters& parameters);

        void
        execute_task() noexcept;

        static void
        tp_timer_callback(PTP_CALLBACK_INSTANCE tp_callback_instance,
                          PVOID                 instance,
                          PTP_TIMER             timer);

        void on_tp_timer(PTP_CALLBACK_INSTANCE) noexcept;

        PTP_TIMER m_timer{};
    };

} // namespace m::threadpool_impl::windows
