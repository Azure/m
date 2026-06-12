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

#include "work_queue_base.h"

namespace m::threadpool_impl
{
    work_queue_base::work_queue_base(work_queue_execution_policy wqep, std::wstring description):
        m_wqep(wqep), m_description(std::move(description))
    {
        //
    }

    std::size_t
    work_queue_base::do_queue_size()
    {
        auto l = std::unique_lock(m_mutex);
        return m_ready_queue.size();
    }

    /// <summary>
    /// Returns a count of the running work items.
    /// </summary>
    /// <returns></returns>
    std::size_t
    work_queue_base::do_running()
    {
        auto l = std::unique_lock(m_mutex);
        return m_running_work_items.size();
    }

    bool
    work_queue_base::do_wait_for(std::chrono::milliseconds const& dur)
    {
        auto l = std::unique_lock(m_mutex);
        return m_cv.wait_for(
            l, dur, [this] { return m_ready_queue.empty() && m_running_work_items.empty(); });
    }

    std::shared_ptr<m::work_item>
    work_queue_base::do_enqueue(std::packaged_task<void()>&& task, m::wsstring const& description)
    {
        auto wi = std::make_shared<m::work_queue_impl::work_item>(description, std::move(task));

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

        return wi;
    }

    void
    work_queue_base::do_close()
    {
        // The drain itself is platform-specific. It cancels not-yet-started
        // work and waits for in-flight callbacks; it must never run on a
        // threadpool callback thread, which both `close()` and the owning
        // destructor guarantee.
        perform_platform_teardown();
    }

} // namespace m::threadpool_impl
