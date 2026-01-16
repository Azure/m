// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <condition_variable>
#include <deque>
#include <list>
#include <map>
#include <mutex>

#include <m/threadpool/work_item_source.h>
#include <m/threadpool/work_queue.h>
#include <m/threadpool/work_queue_execution_policy.h>

#include "work_item.h"

namespace m::threadpool_impl
{
    /// <summary>
    /// A `threadpool_impl::work_queue` is a proxy between a queue of work
    /// items and a Windows Threadpool Work item.
    ///
    /// The work_queue itself has its lifetime managed by a shared_ptr<>
    /// but the windows PTP_WORK has a std::unique_ptr<> around it, and
    /// the single work item is used to invoke the try_work() callback on
    /// the work_queue instance to perform work.
    ///
    /// We expect the windows thread pool to manage parallelism in the
    /// degree to which it issues the callbacks in parallel.
    ///
    /// For work queues that have a non-parallel policy, the queue will
    /// be drained by each callback. [note: there would appear to be
    /// some weird races / clumping that can occur here that we need to
    /// figure out - do additional queued items also issue work?]
    ///
    /// For parallel policy queues the behavior is simpler. Each
    /// callback executes one queue item and then terminates. The Windows
    /// threadpool documentation claims that the number of wakeups is
    /// guaranteed to be equal to the number of calls the
    /// SubmitThreadpoolWork() (up to ULONG_MAX - 2^32-1, not a limit
    /// we shall concern ourselves with at this time).
    ///
    /// </summary>
    class work_queue_base : public m::work_queue
    {
    protected:
        work_queue_base()                           = default;
        ~work_queue_base()                          = default;
        work_queue_base(work_queue_base const&)     = delete;
        work_queue_base(work_queue_base&&) noexcept = delete;
        work_queue_base(work_queue_execution_policy wqep, std::wstring description);

        void
        operator=(work_queue_base const&) = delete;

        work_queue_base&
        operator=(work_queue_base&&) noexcept = delete;

        void
        swap(work_queue_base& other) noexcept = delete;

        std::size_t
        do_queue_size() override;

        std::size_t
        do_running() override;

        bool
        do_wait_for(std::chrono::milliseconds dur) override;

        std::shared_ptr<work_item>
        do_enqueue(std::packaged_task<void()>&& task, m::wsstring const& description) override;

        virtual void
        perform_platform_initialization() = 0;

        /// <summary>
        /// The on_new_work_item() pure virtual member function is overridden by
        /// platform-specific implementations to dispatch to a platform-specific
        /// mechanism to notify a thread to pick up the work and run it.
        ///
        /// It is called with the work queue mutex locked so it should be very
        /// quick.
        /// </summary>
        /// <param name="wi"></param>
        virtual void
        on_new_work_item(std::shared_ptr<m::work_queue_impl::work_item> const& wi) = 0;

        using ready_queue_type  = std::deque<std::shared_ptr<m::work_queue_impl::work_item>>;
        using running_work_type = std::
            map<work_item_id_type, std::shared_ptr<m::work_queue_impl::work_item>, std::less<>>;

        std::mutex                  m_mutex;
        std::condition_variable     m_cv;
        work_queue_execution_policy m_wqep;
        std::wstring                m_description;
        ready_queue_type            m_ready_queue;
        running_work_type           m_running_work_items;
        bool                        m_platform_initialized{false};
    };
} // namespace m::threadpool_impl
