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

#include "threadpool_impl.h"

namespace m::threadpool_impl
{
    class timer : public m::timer
    {
    public:
        using duration = m::threadpool_types::duration;

        struct task_type
        {
            task_type(std::packaged_task<timer_callable>&& task):
                m_discriminant(discriminant::uncancellable), m_tasks(std::move(task))
            {}

            task_type(std::packaged_task<timer_cancellable_callable>&& task):
                m_discriminant(discriminant::cancellable), m_tasks(std::move(task))
            {}

            enum class discriminant
            {
                uncancellable,
                cancellable,
            };

            discriminant m_discriminant;
            struct tasks
            {
                tasks(std::packaged_task<timer_callable>&& task): m_packaged_task(std::move(task))
                {}

                tasks(std::packaged_task<timer_cancellable_callable>&& task):
                    m_cancellable_packaged_task(std::move(task))
                {}

                std::packaged_task<timer_callable>             m_packaged_task;
                std::packaged_task<timer_cancellable_callable> m_cancellable_packaged_task;
            } m_tasks;

            void
            operator()(std::atomic<bool>& cancelled)
            {
                switch (m_discriminant)
                {
                    case discriminant::uncancellable: m_tasks.m_packaged_task(); break;
                    case discriminant::cancellable:
                        m_tasks.m_cancellable_packaged_task(cancelled);
                        break;
                    default: throw std::runtime_error("Should not be able to reach this code path");
                }
            }

            void
            reset()
            {
                switch (m_discriminant)
                {
                    case discriminant::uncancellable: m_tasks.m_packaged_task.reset(); break;
                    case discriminant::cancellable:
                        m_tasks.m_cancellable_packaged_task.reset();
                        break;
                    default: throw std::runtime_error("Should not be able to reach this code path");
                }
            }
        };

        timer(task_type&& task);
        timer(m::threadpool_impl::timer&& other) = delete;
        timer(m::threadpool_impl::timer const&)  = delete;
        ~timer();

        void
        operator=(m::threadpool_impl::timer const&) = delete;

        m::threadpool_impl::timer&
        operator=(m::threadpool_impl::timer&& other) = delete;

    protected:
        bool
        do_cancel_requested() override;

        bool
        do_done() override;

        void
        do_try_cancel() override;

        void
        do_set(duration dur) override;

        struct set_threadpool_timer_ex_parameters
        {
            FILETIME  m_buffer_do_not_pass; // Buffer only, do not pass as pftDueTime
            PFILETIME m_p_ft_due_time;
            DWORD     m_ms_period;
            DWORD     m_ms_window_length;
        };

        static void
        compute_timer_times(duration dur, set_threadpool_timer_ex_parameters& parameters);

        static void
        tp_timer_callback(PTP_CALLBACK_INSTANCE tp_callback_instance,
                          PVOID                 instance,
                          PTP_TIMER             timer);

        void on_tp_timer(PTP_CALLBACK_INSTANCE) noexcept;

        mutable std::mutex m_mutex;
        PTP_TIMER          m_timer{};
        task_type          m_task;
        duration           m_duration;
        std::atomic<bool>  m_cancel_requested{false};
        bool               m_cancelled{false};
        bool               m_done{true};
        bool               m_started{false};
    };

} // namespace m::threadpool_impl
