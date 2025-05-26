// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>

#include <m/utility/pointers.h>

namespace m
{
    namespace threadpool_types
    {
        using duration = std::chrono::microseconds;
    }

    class timer
    {
    public:
        bool
        cancel_requested()
        {
            return do_cancel_requested();
        }

        bool
        done()
        {
            return do_done();
        }

        void
        try_cancel()
        {
            do_try_cancel();
        }

        /// <summary>
        /// The `set` member function is only valid on a timer that is
        /// in the `done` state. Attempting to execute it on a timer that
        /// is in any other state results in undefined behavior.
        ///
        /// Timers begin their existence in the "done" state.
        /// </summary>
        /// <typeparam name="Rep"></typeparam>
        /// <typeparam name="Period"></typeparam>
        /// <param name="dur"></param>
        template <typename Rep, typename Period>
        void
        set(std::chrono::duration<Rep, Period> dur)
        {
            do_set(std::chrono::duration_cast<duration>(dur));
        }

    protected:
        using duration = threadpool_types::duration;

        virtual ~timer() {}

        virtual bool
        do_cancel_requested() = 0;

        virtual bool
        do_done() = 0;

        virtual void
        do_try_cancel() = 0;

        virtual void
        do_set(duration dur) = 0;
    };

    using timer_callable             = void();
    using timer_cancellable_callable = void(std::atomic<bool>&);

    class threadpool_class
    {
    public:
        template <typename F>
        std::shared_ptr<timer>
        create_timer(F&& f)
        {
            return do_create_timer(std::packaged_task<timer_callable>(std::forward<F>(f)));
        }

        template <typename F>
        std::shared_ptr<timer>
        create_cancellable_timer(F&& f)
        {
            return do_create_timer(
                std::packaged_task<timer_cancellable_callable>(std::forward<F>(f)));
        }

    protected:
        virtual ~threadpool_class() = default;

        virtual std::shared_ptr<timer>
        do_create_timer(std::packaged_task<timer_callable>&& task) = 0;

        virtual std::shared_ptr<timer>
        do_create_timer(std::packaged_task<timer_cancellable_callable>&& task) = 0;

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
