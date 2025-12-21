// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <m/chrono/chrono.h>
#include <m/error_handling/macros.h>
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
        virtual ~work_item() {}

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

        std::wstring
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

        virtual std::wstring
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

    namespace work_queue_impl
    {
        inline std::atomic<uint64_t> work_item_id_counter{0};

        class work_item : public m::work_item
        {
        public:
            work_item(work_item const&) = delete;
            work_item(work_item&&)      = delete;

            void
            work()
            {
                do_work();
            }

        protected:
            work_item(std::wstring description);
            work_item();
            ~work_item() = default;

            utc_time_point
            do_enqueue_time() override
            {
                auto l = std::unique_lock(m_mutex);
                return m_work_item_times.m_enqueue_time;
            }

            std::optional<utc_time_point>
            do_start_time() override
            {
                auto l = std::unique_lock(m_mutex);
                return m_work_item_times.m_start_time;
            }

            std::optional<utc_time_point>
            do_end_time() override
            {
                auto l = std::unique_lock(m_mutex);
                return m_work_item_times.m_end_time;
            }

            work_item_times
            do_times() override
            {
                auto l = std::unique_lock(m_mutex);
                return m_work_item_times;
            }

            work_item_state
            do_state() override
            {
                auto l = std::unique_lock(m_mutex);
                return m_work_item_state;
            }

            std::wstring
            do_description() override
            {
                // description is not under the lock, it does not change after construction
                // so we do not take the mutex
                return m_description;
            }

            bool
            do_try_cancel() override
            {
                auto l = std::unique_lock(m_mutex);
                return false;
            }

            uint64_t
            do_id() override
            {
                return m_id;
            }

            virtual void
            do_work() = 0;

            work_item_id_type m_id;          // immutable once constructed
            std::wstring      m_description; // immutable once constructed
            std::mutex        m_mutex;
            work_item_times   m_work_item_times;
            work_item_state   m_work_item_state;
        };

        /// <summary>
        /// The `work_item` class is the concrete implementation
        /// of the `work_item` interface type, from which individual
        /// work_item
        /// </summary>
        template <typename R, typename... Args>
            requires(std::is_void_v<R> || std::regular<R>)
        class runnable_work_item : public work_item
        {
        public:
            runnable_work_item()                          = delete;
            runnable_work_item(runnable_work_item const&) = delete;
            runnable_work_item(runnable_work_item&&)      = delete;
            template <typename Fn>
                requires(std::invocable<Fn, Args...>)
            runnable_work_item(std::wstring description, Fn&& fn, Args&&... args):
                work_item(std::move(description)),
                m_packaged_task(std::forward<Fn>(fn), std::forward<Args>(args)...),
                m_future(m_packaged_task.get_future())
            {}

            template <typename Fn>
                requires(std::invocable<Fn, Args...>)
            runnable_work_item(Fn&& fn, Args&&... args):
                m_packaged_task(std::forward<Fn>(fn), std::forward<Args>(args)...),
                m_future(m_packaged_task.get_future())
            {}

            ~runnable_work_item() {}

            /// <summary>
            /// gets the result of the task having run. Will throw any
            /// exception that had occurred during the execution of the task.
            /// </summary>
            /// <returns></returns>
            R
            get()
            {
                return m_future.get();
            }

        private:
            void
            do_work() noexcept override
            {
                // Before execution:
                //
                // If the work item was canceled, just bail out.
                //
                // Make sure that state is queued.
                //
                // Record the start time
                // set the state to running.
                //
                {
                    auto l = std::unique_lock(m_mutex);

                    if (m_work_item_state == work_item_state::canceled)
                        return;

                    M_INTERNAL_ERROR_CHECK(m_work_item_state == work_item_state::queued);

                    m_work_item_times.m_start_time = m::clock::now();
                    m_work_item_state              = work_item_state::running;
                }

                m_packaged_task();

                // After execution:
                //
                // Make sure that state is still running.
                //
                // Record the end time
                // set the state to done.
                //
                {
                    auto l = std::unique_lock(m_mutex);

                    M_INTERNAL_ERROR_CHECK(m_work_item_state == work_item_state::running);

                    m_work_item_times.m_end_time = m::clock::now();
                    m_work_item_state            = work_item_state::done;
                }
            }

            void
            do_wait() override
            {
                m_future.wait();
            }

            bool
            do_wait_for(std::chrono::milliseconds const d) override
            {
                auto const future_status = m_future.wait_for(d);

                switch (future_status)
                {
                    default: M_UNREACHABLE_CODE(); break;

                    case std::future_status::deferred: return true;
                    case std::future_status::ready: return true;
                    case std::future_status::timeout: return false;
                }
            }

            bool
            do_wait_until(m::time_point const tp) override
            {
                auto const future_status = m_future.wait_until(tp);

                switch (future_status)
                {
                    default: M_UNREACHABLE_CODE(); break;

                    case std::future_status::deferred: return true;
                    case std::future_status::ready: return true;
                    case std::future_status::timeout: return false;
                }
            }

            std::packaged_task<R(Args...)> m_packaged_task;
            std::future<R>                 m_future;
        };
    } // namespace work_queue_impl

    /// <summary>
    /// A work queue is a separate
    /// </summary>
    class work_queue
    {
    public:
        virtual ~work_queue() {}

        template <typename R, typename Fn, typename... Args>
            requires(std::invocable<Fn, Args...>)
        std::shared_ptr<work_item>
        enqueue(Fn&& fn, Args&&... args)
        {
            auto wi = std::make_shared<work_queue_impl::runnable_work_item<R, Args...>>(
                std::forward<Fn>(fn), std::forward<Args>(args)...);

            do_enqueue(wi);

            return wi;
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
            return do_wait_until(m::time_point_cast(when));
        }

    private:
        virtual std::size_t
        do_queue_size() = 0;

        virtual std::size_t
        do_running() = 0;

        virtual bool
        do_wait_for(std::chrono::milliseconds dur) = 0;

        virtual bool
        do_wait_until(m::time_point when) = 0;

        virtual void
        do_enqueue(std::shared_ptr<m::work_queue_impl::work_item> wi) = 0;
    };

} // namespace m
