// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <optional>

#include <m/chrono/chrono.h>
#include <m/threadpool/types.h>
#include <m/threadpool/work_item_state.h>
#include <m/utility/pointers.h>

namespace m
{
    struct work_item_times
    {
        time_point                m_enqueue_time;
        std::optional<time_point> m_start_time;
        std::optional<time_point> m_end_time;
    };

    class work_item
    {
    public:
        time_point
        enqueue_time()
        {
            return do_enqueue_time();
        }

        std::optional<time_point>
        start_time()
        {
            return do_start_time();
        }

        std::optional<time_point>
        end_time()
        {
            return do_end_time();
        }

        /// <summary>
        /// The `times()` member function returns all three times for
        /// a work item:
        ///
        /// - Enqueue time
        /// - Start time if it has started
        /// - End time if it has ended
        ///
        /// atomically
        /// </summary>
        /// <returns>The `work_item_times` struct containing the data requested.</returns>
        work_item_times
        times()
        {
            return do_times();
        }

        work_item_state
        state()
        {
            return do_state();
        }

        /// <summary>
        /// The `try_cancel` member function _attempts_ to cancel the work item.
        ///
        /// Returns `true` if the work item is successfully cancelled before
        /// starting, `false` otherwise.
        ///
        /// If the work item has already started executing, or has completed execution,
        /// or cannot be cancelled for any other reason, false is returned. Do not
        /// infer that the work item is running because it is not able to be cancelled,
        /// although that is the most likely cause.
        /// </summary>
        /// <returns>`true` if the work item is cancelled, `false` otherwise.</returns>
        bool
        try_cancel()
        {
            return do_try_cancel();
        }

        std::shared_future<void>
        shared_future()
        {
            return do_shared_future();
        }

        // Waits etc are implemented by getting a std::shared_future from the
        // work item and then waiting on it.

    private:
        virtual time_point
        do_enqueue_time() = 0;

        virtual std::optional<time_point>
        do_start_time() = 0;

        virtual std::optional<time_point>
        do_end_time() = 0;

        virtual work_item_times
        do_times() = 0;

        virtual work_item_state
        do_state() = 0;

        virtual bool
        do_try_cancel() = 0;

        virtual std::shared_future<void>
        do_shared_future() = 0;
    };

    /// <summary>
    /// A work queue is a separate
    /// </summary>
    class work_queue
    {
    public:
        template <typename F, typename... Args>
        std::shared_ptr<work_item>
        enqueue(F&& f, std::wformat_string<Args...>&& fmt, Args&&... args)
        {
            auto description = std::format(std::forward<std::wformat_string<Args...>>(fmt),
                                           std::forward<Args>(args)...);

            return do_enqueue(std::packaged_task<work_item_callable>(std::forward<F>(f), std::move(description));
        }

        template <typename F>
        std::shared_ptr<work_item>
        enqueue(F&& f)
        {
            return do_enqueue(std::packaged_task<work_item_callable>(std::forward<F>(f));
        }

        std::size_t
        queue_size()
        {
            return do_queue_size();
        }

        std::size_t
        running()
        {
            return do_running();
        }

        /// <summary>
        /// Waits for the work items to complete, or for `dur` to expire. Returns `true`
        /// if the work item completes, `false` if the duration passes without the
        /// work item completing.
        /// </summary>
        /// <typeparam name="Rep">The representation of the duration passed in. Usually not
        /// specified, the compiler will infer from the concrete duration passed.</typeparam>
        /// <typeparam name="Period">The period of the duration passed in.
        /// Usually not specified, the compiler will infer from the concrete
        /// duration passed in.</typeparam>
        /// <param name="dur">The duration to wait before abandoning the wait.</param>
        /// <returns>`true` if the task completed within the duration, `false` if the
        /// task did not complete within the time period.</returns>
        template <typename Rep, typename Period>
        bool
        wait_for(std::chrono::duration<Rep, Period> dur)
        {
            return do_wait_for(std::chrono::duration_cast<std::chrono::milliseconds>(dur));
        }

        /// <summary>
        /// Waits for the work items to complete, or for `when` to arrive. Returns `true`
        /// if the work item completes, `false` if the time comes without the
        /// work item completing.
        /// </summary>
        /// <typeparam name="Clock"></typeparam>
        /// <typeparam name="Duration"></typeparam>
        /// <param name="when">The time point to wait until.</param>
        /// <returns>`true` if the task completed before the time point, `false` if the
        /// task did not complete by `when`.</returns>
        template <typename Clock, typename Duration>
        bool
        wait_until(std::chrono::time_point<Clock, Duration> when)
        {
            return do_wait_until(std::chrono::clock_cast<utc_clock>(when));
        }

    private:
        virtual std::shared_ptr<work_item>
        do_enqueue(std::packaged_task<work_item_callable>&& work_item_fn) = 0;

        virtual std::shared_ptr<work_item>
        do_enqueue(std::packaged_task<work_item_callable>&& work_item_fn, std::wstring&& description) = 0;

        virtual std::size_t
        do_queue_size() = 0;

        virtual std::size_t
        do_running() = 0;

        virtual bool
        do_wait_for(std::chrono::milliseconds dur) = 0;

        virtual bool
        do_wait_until(time_point when) = 0;
    };

} // namespace m
