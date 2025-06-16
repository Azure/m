// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <variant>

#include "threadpool_timer_impl.h"

m::threadpool_impl::timer::timer(m::threadpool_impl::timer::task_type&& task,
                                 std::wstring&&                         description):
    m_task(std::move(task)), m_description(std::move(description))
{
    //
}

m::threadpool_impl::timer::~timer()
{
    //
}

bool
m::threadpool_impl::timer::do_cancel_requested()
{
    auto l = std::unique_lock(m_mutex);
    return m_cancel_requested;
}

bool
m::threadpool_impl::timer::do_done()
{
    auto l = std::unique_lock(m_mutex);
    return m_done;
}

void
m::threadpool_impl::timer::do_try_cancel()
{
    // Can't try to hold the mutex here since the mutex is held over the
    // task execution!
    m_cancel_requested.store(true, std::memory_order_release);
}

void
m::threadpool_impl::timer::do_set(duration dur)
{
    auto l = std::unique_lock(m_mutex);

    m_duration = dur;
    m_task.reset();
}
