// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>

#include <m/threadpool/periodic_timer.h>
#include <m/threadpool/timer.h>
#include <m/threadpool/types.h>
#include <m/threadpool/work_queue.h>
#include <m/threadpool/work_queue_execution_policy.h>
#include <m/utility/pointers.h>

namespace m
{
    class threadpool_class
    {
    public:
        template <typename F>
        std::shared_ptr<timer>
        create_timer(F&& f)
        {
            return do_create_timer(std::packaged_task<timer_normal_callable>(std::forward<F>(f)));
        }

        template <typename F, typename... Args>
        std::shared_ptr<timer>
        create_timer(F&& f, std::wformat_string<Args...>&& fmt, Args&&... args)
        {
            auto description = std::format(std::forward<std::wformat_string<Args...>>(fmt),
                                           std::forward<Args>(args)...);
            return do_create_timer(std::packaged_task<timer_normal_callable>(std::forward<F>(f)),
                                   std::move(description));
        }

        template <typename F>
        std::shared_ptr<timer>
        create_cancellable_timer(F&& f)
        {
            return do_create_cancellable_timer(
                std::packaged_task<timer_cancellable_callable>(std::forward<F>(f)));
        }

        template <typename F, typename... Args>
        std::shared_ptr<timer>
        create_cancellable_timer(F&& f, std::wformat_string<Args...>&& fmt, Args&&... args)
        {
            auto description = std::format(std::forward<std::wformat_string<Args...>>(fmt),
                                           std::forward<Args>(args)...);
            return do_create_cancellable_timer(
                std::packaged_task<timer_cancellable_callable>(std::forward<F>(f)),
                std::move(description));
        }

        template <typename F>
        std::shared_ptr<periodic_timer>
        create_periodic_timer(F&& f)
        {
            return do_create_periodic_timer(
                std::packaged_task<timer_normal_callable>(std::forward<F>(f)));
        }

        template <typename F, typename... Args>
        std::shared_ptr<periodic_timer>
        create_periodic_timer(F&& f, std::wformat_string<Args...>&& fmt, Args&&... args)
        {
            auto description = std::format(std::forward<std::wformat_string<Args...>>(fmt),
                                           std::forward<Args>(args)...);
            return do_create_periodic_timer(
                std::packaged_task<timer_normal_callable>(std::forward<F>(f)),
                std::move(description));
        }

        std::shared_ptr<work_queue>
        create_work_queue(
            work_queue_execution_policy wqep = m::work_queue_execution_policy::parallel)
        {
            return do_create_work_queue(wqep, L"");
        }

        template <typename... Args>
        std::shared_ptr<work_queue>
        create_work_queue(work_queue_execution_policy    wqep,
                          std::wformat_string<Args...>&& fmt,
                          Args&&... args)
        {
            auto description = std::format(std::forward<std::wformat_string<Args...>>(fmt),
                                           std::forward<Args>(args)...);

            return do_create_work_queue(wqep, std::move(description));
        }

    protected:
        virtual ~threadpool_class() = default;

        virtual std::shared_ptr<timer>
        do_create_timer(std::packaged_task<timer_normal_callable>&& task) = 0;

        virtual std::shared_ptr<timer>
        do_create_timer(std::packaged_task<timer_normal_callable>&& task,
                        std::wstring                                description) = 0;

        virtual std::shared_ptr<timer>
        do_create_cancellable_timer(std::packaged_task<timer_cancellable_callable>&& task) = 0;

        virtual std::shared_ptr<timer>
        do_create_cancellable_timer(std::packaged_task<timer_cancellable_callable>&& task,
                                    std::wstring description) = 0;

        virtual std::shared_ptr<periodic_timer>
        do_create_periodic_timer(std::packaged_task<timer_normal_callable>&& task) = 0;

        virtual std::shared_ptr<periodic_timer>
        do_create_periodic_timer(std::packaged_task<timer_normal_callable>&& task,
                                 std::wstring                                description) = 0;

        virtual std::shared_ptr<work_queue>
        do_create_work_queue(work_queue_execution_policy wqep, std::wstring description) = 0;

        friend class timer;
    };

    // Implemented by platforms to allow for e.g. Windows vs. Linux support
    std::shared_ptr<threadpool_class>
    make_platform_default_threadpool();

    struct global_threadpool_type
    {
        static std::shared_ptr<threadpool_class>
        get()
        {
            struct inner
            {
                inner() { m_threadpool_class = make_platform_default_threadpool(); }

                std::shared_ptr<threadpool_class> m_threadpool_class;
            };

            static inner s_inner;

            return s_inner.m_threadpool_class;
        }
    };

    // Enable m::threadpool::create_timer(x) etc.
    inline std::shared_ptr<threadpool_class> threadpool = global_threadpool_type::get();
} // namespace m
