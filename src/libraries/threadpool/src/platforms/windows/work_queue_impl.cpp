// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <tuple>

#include <m/threadpool/threadpool.h>
#include <m/threadpool/work_item_state.h>

#include "work_queue_impl.h"

namespace m::threadpool_impl
{
    work_queue::work_queue(work_queue&& other) noexcept
    {
        using std::swap;

        swap(m_queue, other.m_queue);
    }

    work_queue&
    work_queue::operator=(work_queue&& other) noexcept
    {
        using std::swap;

        swap(m_queue, other.m_queue);
    }

    void
    work_queue::swap(work_queue& other) noexcept
    {
        using std::swap;

        swap(m_queue, other.m_queue);
    }

    std::shared_ptr<work_item>
    work_queue::do_enqueue(work_fn&& f)
    {
        //
    }

    std::shared_ptr<work_item>
    work_queue::do_enqueue(work_fn const& f)
    {
        //
    }

    std::size_t
    work_queue::do_queue_size()
    {
        auto l = std::unique_lock(m_mutex);
        return m_queue.size();
    }

    std::size_t
    work_queue::do_running()
    {
        auto        l = std::unique_lock(m_mutex);
        std::size_t c{};

        for (auto&& e: m_queue)
        {
            if (e->state() == work_item_state::running)
                c++;
        }

        return c;
    }

    bool
    work_queue::do_wait_for(std::chrono::milliseconds dur)
    {
        auto l = std::unique_lock(m_mutex);
        return m_cv.wait_for(l, dur, [this] { m_queue.empty(); });
    }

    bool
    work_queue::do_wait_until(time_point when)
    {
        auto l = std::unique_lock(m_mutex);
        return m_cv.wait_until(l, when, [this] { m_queue.empty(); });
    }

    //
} // namespace m::threadpool_impl
