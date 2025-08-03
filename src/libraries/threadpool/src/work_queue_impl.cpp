// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <tuple>

#include <m/error_handling/macros.h>
#include <m/exception/exception.h>
#include <m/thread_description/thread_description.h>
#include <m/threadpool/threadpool.h>
#include <m/threadpool/work_item_state.h>
#include <m/threadpool/work_queue_execution_policy.h>

#include "work_queue_impl.h"

namespace m::threadpool_impl
{
    work_queue::work_queue(work_queue_execution_policy wqep, std::wstring description):
        m_wqep(wqep), m_description(std::move(description))
    {
        //
    }

    std::size_t
    work_queue::do_queue_size()
    {
        auto l = std::unique_lock(m_mutex);
        return m_ready_queue.size();
    }

    /// <summary>
    /// Returns a count of the running work items.
    /// </summary>
    /// <returns></returns>
    std::size_t
    work_queue::do_running()
    {
        auto l = std::unique_lock(m_mutex);
        return m_running_work_items.size();
    }

    bool
    work_queue::do_wait_for(std::chrono::milliseconds dur)
    {
        auto l = std::unique_lock(m_mutex);
        return m_cv.wait_for(
            l, dur, [this] { return m_ready_queue.empty() && m_running_work_items.empty(); });
    }

    bool
    work_queue::do_wait_until(utc_time_point when)
    {
        auto l = std::unique_lock(m_mutex);
        return m_cv.wait_until(
            l, when, [this] { return m_ready_queue.empty() && m_running_work_items.empty(); });
    }

    void
    work_queue::do_enqueue(std::shared_ptr<m::work_queue_impl::work_item> wi)
    {
        auto l = std::unique_lock(m_mutex);

        if (!m_platform_initialized)
        {
            perform_platform_initialization();
            m_platform_initialized = true;
        }

        m_ready_queue.emplace_back(wi);

        l.unlock();

        on_new_work_item(wi);

        m_cv.notify_all();
    }

} // namespace m::threadpool_impl
