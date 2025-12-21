// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string>
#include <tuple>

#include <m/threadpool/threadpool.h>

#include "periodic_timer.h"
#include "threadpool.h"
#include "timer.h"
#include "work_queue.h"

using namespace std::string_literals;

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

    std::unique_ptr<m::timer>
    threadpool::do_create_timer(std::packaged_task<timer_callable>&& task)
    {
        return std::make_unique<m::threadpool_impl::timer>(std::move(task), L""s);
    }

    std::unique_ptr<m::timer>
    threadpool::do_create_timer(std::packaged_task<timer_callable>&& task, std::wstring description)
    {
        return std::make_unique<m::threadpool_impl::timer>(std::move(task), description);
    }

    std::unique_ptr<m::periodic_timer>
    threadpool::do_create_periodic_timer(std::packaged_task<timer_callable>&& task)
    {
        return std::make_unique<m::threadpool_impl::periodic_timer>(std::move(task), L""s);
    }

    std::unique_ptr<m::periodic_timer>
    threadpool::do_create_periodic_timer(std::packaged_task<timer_callable>&& task,
                                         std::wstring                         description)
    {
        return std::make_unique<m::threadpool_impl::periodic_timer>(std::move(task), description);
    }

    std::shared_ptr<m::work_queue>
    threadpool::do_create_work_queue(m::work_queue_execution_policy wqep, std::wstring description)
    {
        return std::make_shared<m::threadpool_impl::work_queue>(wqep, description);
    }
} // namespace m::threadpool_impl

std::shared_ptr<m::threadpool_class>
m::make_platform_default_threadpool()
{
    return std::make_shared<m::threadpool_impl::threadpool>();
}
