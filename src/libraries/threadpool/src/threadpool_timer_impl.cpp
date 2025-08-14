// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <chrono>
#include <variant>

#include <m/thread_description/thread_description.h>

#include "threadpool_timer_impl.h"

using namespace std::chrono_literals;

namespace m::threadpool_impl
{
    timer::timer(normal_timer_tag_t,
                 std::packaged_task<timer_normal_callable>&& task,
                 std::wstring                                description):
        m_task_type(task_type::normal),
        m_normal_packaged_task(std::move(task)),
        m_description(std::move(description))
    {}

    timer::timer(cancellable_timer_tag_t,
                 std::packaged_task<timer_cancellable_callable>&& task,
                 std::wstring                                     description):
        m_task_type(task_type::cancellable),
        m_cancellable_packaged_task(std::move(task)),
        m_description(std::move(description))
    {}

    timer::timer(periodic_timer_tag_t,
                 std::packaged_task<timer_normal_callable>&& task,
                 std::wstring                                description):
        m_task_type(task_type::periodic),
        m_normal_packaged_task(std::move(task)),
        m_description(std::move(description))
    {}

    timer::~timer() {}

    bool
    timer::do_cancel_requested()
    {
        auto l = std::unique_lock(m_mutex);
        return m_cancel_requested;
    }

    bool
    timer::do_done()
    {
        return m_done.load(std::memory_order_acquire);
    }

    void
    timer::do_try_cancel()
    {
        // Can't try to hold the mutex here since the mutex is held over the
        // task execution!
        m_cancel_requested.store(true, std::memory_order_release);
    }

    bool
    timer::do_set()
    {
        auto l = std::unique_lock(m_mutex);
        return m_started;
    }
} // namespace m::threadpool_impl