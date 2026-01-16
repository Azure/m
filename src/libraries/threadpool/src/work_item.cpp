// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <tuple>

#include <m/error_handling/macros.h>
#include <m/threadpool/threadpool.h>
#include <m/threadpool/work_item_state.h>
#include <m/threadpool/work_queue.h>
#include <m/threadpool/work_queue_execution_policy.h>

#include "work_item.h"
#include "work_queue_base.h"

namespace m::work_queue_impl
{
    work_item::work_item(m::wsstring description, std::packaged_task<void()>&& task):
        m_id(work_queue_impl::work_item_id_counter.fetch_add(1)),
        m_description(std::move(description)),
        m_work_item_state(work_item_state::queued),
        m_packaged_task(std::move(task)),
        m_future(m_packaged_task.get_future())
    {
        m_work_item_times.m_enqueue_time = utc_time_point::clock::now();
    }

    utc_time_point
    work_item::do_enqueue_time()
    {
        auto l = std::unique_lock(m_mutex);
        return m_work_item_times.m_enqueue_time;
    }

    std::optional<utc_time_point>
    work_item::do_start_time()
    {
        auto l = std::unique_lock(m_mutex);
        return m_work_item_times.m_start_time;
    }

    std::optional<utc_time_point>
    work_item::do_end_time()
    {
        auto l = std::unique_lock(m_mutex);
        return m_work_item_times.m_end_time;
    }

    work_item_times
    work_item::do_times()
    {
        auto l = std::unique_lock(m_mutex);
        return m_work_item_times;
    }

    work_item_state
    work_item::do_state()
    {
        auto l = std::unique_lock(m_mutex);
        return m_work_item_state;
    }

    m::not_null<m::cwzstring>
    work_item::do_description()
    {
        // description is not under the lock, it does not change after construction
        // so we do not take the mutex
        return m_description.c_str();
    }

    bool
    work_item::do_try_cancel()
    {
        auto l = std::unique_lock(m_mutex);
        return false;
    }

    uint64_t
    work_item::do_id()
    {
        return m_id;
    }

    void
    work_item::do_work() noexcept
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
    work_item::do_wait()
    {
        m_future.wait();
    }

    bool
    work_item::do_wait_for(std::chrono::milliseconds const d)
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
    work_item::do_wait_until(m::time_point const tp)
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

} // namespace m::work_queue_impl
