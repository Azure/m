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

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

#include "../../work_queue_impl.h"
#include "tp_work.h"

namespace m::windows_threadpool_impl
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
    class work_queue :
        public m::threadpool_impl::work_queue,
        public std::enable_shared_from_this<m::windows_threadpool_impl::work_queue>
    {
    public:
        work_queue()                      = default;
        ~work_queue()                     = default;
        work_queue(work_queue const&)     = delete;
        work_queue(work_queue&&) noexcept = delete;
        work_queue(m::work_queue_execution_policy wqep, std::wstring description);

        void
        operator=(work_queue const&) = delete;
        work_queue&
        operator=(work_queue&&) noexcept = delete;

        void
        swap(work_queue& other) noexcept = delete;

    private:
        void
        perform_platform_initialization() override;

        void
        on_new_work_item(std::shared_ptr<m::work_queue_impl::work_item> const& wi) override;

        struct callback_context
        {
            std::weak_ptr<m::windows_threadpool_impl::work_queue> m_work_queue;
        };

        static void
        static_tp_work_callback(PTP_CALLBACK_INSTANCE instance,
                                PVOID                 context,
                                PTP_WORK              work) noexcept;

        void
        tp_work_callback() noexcept;

        tp_work                           m_tp_work;
        std::unique_ptr<callback_context> m_callback_context;
    };
} // namespace m::windows_threadpool_impl
