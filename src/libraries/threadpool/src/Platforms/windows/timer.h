// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include <Windows.h>

#include <m/threadpool/threadpool.h>
#include <m/utility/pointers.h>
#include <m/win32/threadpool.h>

#include "threadpool.h"

namespace m::threadpool_impl
{
    //
    // Windows FILETIME represents 10ns units
    //
    // using filetime_ratio = std::ratio<1, 100000000>;
    using filetime_ratio = std::ratio<1, 10000000>;

    using file_time = std::chrono::duration<long long, filetime_ratio>;

    class timer : public m::timer
    {
    public:
        timer(std::packaged_task<timer_callable>&& task, std::wstring description);

        timer(m::threadpool_impl::timer&& other) = delete;
        timer(m::threadpool_impl::timer const&)  = delete;
        ~timer();

        void
        operator=(m::threadpool_impl::timer const&) = delete;

        m::threadpool_impl::timer&
        operator=(m::threadpool_impl::timer&& other) = delete;

    protected:
        bool
        do_is_set() override;

        void
        do_set(duration dur) override;

        void
        do_cancel() override;

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
                          PTP_TIMER             timer);

        void on_tp_timer(PTP_CALLBACK_INSTANCE) noexcept;

        //
        // State management is somewhat tricky here. We can't really hold
        // the locks over calling out to things because that would deadlock.
        //
        //
        //
        mutable std::shared_mutex      m_mutex;
        m::win32::threadpool::tp_timer m_timer;

        std::packaged_task<timer_callable> m_packaged_task;
        duration                           m_duration;
        std::wstring                       m_description;
        std::uintmax_t                     m_version{};
        std::uintmax_t                     m_version_at_dispatch{};
        std::uintmax_t                     m_version_done{};
        bool                               m_cancel_on_wait{false};
        bool                               m_in_dispatch{false};
    };

} // namespace m::threadpool_impl
