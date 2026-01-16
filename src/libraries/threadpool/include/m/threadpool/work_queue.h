// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <atomic>
#include <chrono>
#include <format>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <m/chrono/chrono.h>
#include <m/error_handling/macros.h>
#include <m/sstring/sstring.h>
#include <m/threadpool/types.h>
#include <m/threadpool/work_item_state.h>
#include <m/utility/pointers.h>

namespace m
{
    using work_item_id_type = uint64_t;

    struct work_item_times
    {
        utc_time_point                m_enqueue_time;
        std::optional<utc_time_point> m_start_time;
        std::optional<utc_time_point> m_end_time;
    };

    class work_item
    {
    public:
        virtual ~work_item() = default;

        utc_time_point
        enqueue_time()
        {
            return do_enqueue_time();
        }

        std::optional<utc_time_point>
        start_time()
        {
            return do_start_time();
        }

        std::optional<utc_time_point>
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

        m::not_null<m::cwzstring>
        description()
        {
            return do_description();
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

        work_item_id_type
        id()
        {
            return do_id();
        }

        void
        wait()
        {
            do_wait();
        }

        template <typename Rep, typename Period>
        bool
        wait_for(std::chrono::duration<Rep, Period> const& d)
        {
            return do_wait_for(std::chrono::duration_cast<std::chrono::milliseconds>(d));
        }

        template <typename Clock, typename Duration>
        bool
        wait_until(std::chrono::time_point<Clock, Duration> const& tp)
        {
            return do_wait_until(m::time_point_cast(tp));
        }

    private:
        virtual utc_time_point
        do_enqueue_time() = 0;

        virtual std::optional<utc_time_point>
        do_start_time() = 0;

        virtual std::optional<utc_time_point>
        do_end_time() = 0;

        virtual work_item_times
        do_times() = 0;

        virtual work_item_state
        do_state() = 0;

        virtual m::not_null<m::cwzstring>
        do_description() = 0;

        virtual bool
        do_try_cancel() = 0;

        virtual work_item_id_type
        do_id() = 0;

        virtual void
        do_wait() = 0;

        virtual bool
        do_wait_for(std::chrono::milliseconds d) = 0;

        virtual bool
        do_wait_until(m::time_point tp) = 0;
    };

    /// <summary>
    /// A work queue is a separate execution unit for work items. Work queues may in
    /// the future have policies applied to them regarding the number of concurrent
    /// work items, or how they are scheduled to processors, etc.
    /// </summary>
    class work_queue
    {
    public:
        virtual ~work_queue() = default;

        template <typename Fn>
            requires(std::invocable<Fn>)
        [[nodiscard]]
        std::shared_ptr<work_item>
        enqueue(Fn&& fn)
        {
            auto const static empty_wsstring = m::wsstring(L""sv);

            return do_enqueue(std::packaged_task<void()>(std::forward<Fn>(fn)), empty_wsstring);
        }

        template <typename Fn, typename... Args>
            requires(std::invocable<Fn>)
        [[nodiscard]]
        std::shared_ptr<work_item>
        enqueue(Fn&& fn, std::wformat_string<Args...> fmt, Args&&... args)
        {
            return do_enqueue(std::packaged_task<void()>(std::forward<Fn>(fn)),
                              m::wsstring(std::vformat(
                                  fmt.get(), std::make_wformat_args(std::forward<Args>(args)...))));
        }

        /// <summary>
        /// The `queue_size` member function returns the number of work items
        /// currently in the queue. Not all work items in the queue are necessarily
        /// ready to be executed.
        /// </summary>
        /// <returns>The count of work items in the queue.</returns>
        std::size_t
        queue_size()
        {
            return do_queue_size();
        }

        /// <summary>
        /// Gets the number of currently running items.
        /// </summary>
        /// <returns>The count of running items.</returns>
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

        //
        // "wait" and "wait_until" are omitted, intentionally. This is because
        // waiting indefinitely for a potentially large number of work items
        // to complete is fraught with peril and not of the good kind.
        //
        // If you want to wait indefinitely, use wait_for() with some
        // reasonable duration (1sec? 5sec? 60sec?) in a loop, logging some
        // progress regarding the number of remaining work items in the queue
        // (`running()`, `queue_size()`) as you proceed, in order to assist
        // diagnosibility of why a "hang" is occurring in your production
        // scenarios.
        //

    private:
        virtual std::size_t
        do_queue_size() = 0;

        virtual std::size_t
        do_running() = 0;

        virtual bool
        do_wait_for(std::chrono::milliseconds dur) = 0;

        virtual std::shared_ptr<work_item>
        do_enqueue(std::packaged_task<void()>&& task, m::wsstring const& description) = 0;
    };

} // namespace m
