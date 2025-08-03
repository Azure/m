// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <tuple>

#include <m/threadpool/threadpool.h>

#include "work_item_impl.h"

namespace m::threadpool_impl
{
    work_item::work_item(): m_state(work_item_state::queued) {}

    work_item::work_item(work_item&& other) noexcept
    {
        using std::swap;

        swap(m_work_fn, other.m_work_fn);
        swap(m_work_item_times, other.m_work_item_times);
        swap(m_state, other.m_state);
    }

    work_item&
    work_item::operator=(work_item&& other) noexcept
    {
        using std::swap;

        swap(m_work_fn, other.m_work_fn);
        swap(m_work_item_times, other.m_work_item_times);
        swap(m_state, other.m_state);

        return *this;
    }

    void
    work_item::swap(work_item& other) noexcept
    {
        using std::swap;

        swap(m_work_fn, other.m_work_fn);
        swap(m_work_item_times, other.m_work_item_times);
        swap(m_state, other.m_state);
    }

    time_point
    work_item::do_enqueue_time()
    {
        auto l = std::unique_lock(m_mutex);
        return m_work_item_times.m_enqueue_time;
    }

    std::optional<time_point>
    work_item::do_start_time()
    {
        auto l = std::unique_lock(m_mutex);
        return m_work_item_times.m_start_time;
    }

    std::optional<time_point>
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
        return m_state;
    }

    bool
    work_item::do_try_cancel()
    {
        auto l = std::unique_lock(m_mutex);

        if (m_state == work_item_state::queued)
        {
            m_state = work_item_state::canceled;
            return true;
        }

        return false;
    }

    std::shared_future<void>
        work_item::do_shared_future()
    {
        auto l = std::unique_lock(m_mutex);
        return m_shared_future;
    }

    bool
    work_item::is_done()
    {
        return m_state == work_item_state::done || m_state == work_item_state::canceled;
    }

    void work_item::run()
    {
        // TODO: something about exceptions?
        std::invoke(m_work_fn);
    }

    //
} // namespace m::threadpool_impl
