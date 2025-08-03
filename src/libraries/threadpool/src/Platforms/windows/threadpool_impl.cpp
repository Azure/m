// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <tuple>

#include <m/threadpool/threadpool.h>

#include "threadpool_impl.h"
#include "threadpool_timer_impl.h"
#include "windows_work_queue_impl.h"

namespace m::threadpool_impl
{
    threadpool::threadpool(threadpool&& other) noexcept
    {
        using std::swap;

        // no state to move!
        std::ignore = other;
    }

    threadpool&
    threadpool::operator=(threadpool&& other) noexcept
    {
        using std::swap;

        // no state to move!
        std::ignore = other;

        return *this;
    }

    void
    threadpool::swap(threadpool& other) noexcept
    {
        using std::swap;

        // no state to move!
        std::ignore = other;
    }

    std::shared_ptr<m::timer>
    threadpool::do_create_timer(std::packaged_task<timer_cancellable_callable>&& task,
                                std::wstring                                   description)
    {
        return std::make_shared<m::threadpool_impl::timer>(
            m::threadpool_impl::timer::task_type(std::move(task)), description);
    }

    std::shared_ptr<m::timer>
    threadpool::do_create_timer(std::packaged_task<timer_callable>&& task,
                                std::wstring                       description)
    {
        return std::make_shared<m::threadpool_impl::timer>(
            m::threadpool_impl::timer::task_type(std::move(task)), description);
    }

    std::shared_ptr<m::work_queue>
    threadpool::do_create_work_queue(m::work_queue_execution_policy wqep, std::wstring description)
    {
        return std::make_shared<m::windows_threadpool_impl::work_queue>(wqep, description);
    }
} // namespace m::threadpool_impl

std::shared_ptr<m::threadpool_class>
m::make_platform_default_threadpool()
{
    return std::make_shared<m::threadpool_impl::threadpool>();
}
