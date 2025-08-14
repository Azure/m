// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string>
#include <tuple>

#include <m/threadpool/threadpool.h>

#include "threadpool_impl.h"
#include "linux_threadpool_timer_impl.h"
#include "linux_work_queue_impl.h"

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

    std::shared_ptr<m::timer>
    threadpool::do_create_timer(std::packaged_task<timer_normal_callable>&& task)
    {
        return std::make_shared<m::threadpool_impl::linux_impl::timer>(
            m::threadpool_impl::timer::normal_timer_tag, std::move(task), L""s);
    }

    std::shared_ptr<m::timer>
    threadpool::do_create_timer(std::packaged_task<timer_normal_callable>&& task,
                                std::wstring                                description)
    {
        return std::make_shared<m::threadpool_impl::linux_impl::timer>(
            m::threadpool_impl::timer::normal_timer_tag, std::move(task), description);
    }

    std::shared_ptr<m::timer>
    threadpool::do_create_cancellable_timer(std::packaged_task<timer_cancellable_callable>&& task)
    {
        return std::make_shared<m::threadpool_impl::linux_impl::timer>(
            m::threadpool_impl::timer::cancellable_timer_tag, std::move(task), L""s);
    }

    std::shared_ptr<m::timer>
    threadpool::do_create_cancellable_timer(std::packaged_task<timer_cancellable_callable>&& task,
                                            std::wstring description)
    {
        return std::make_shared<m::threadpool_impl::linux_impl::timer>(
            m::threadpool_impl::timer::cancellable_timer_tag, std::move(task), description);
    }

    std::shared_ptr<m::periodic_timer>
    threadpool::do_create_periodic_timer(std::packaged_task<timer_normal_callable>&& task)
    {
        return std::make_shared<m::threadpool_impl::linux_impl::timer>(
            m::threadpool_impl::timer::periodic_timer_tag, std::move(task), L""s);
    }

    std::shared_ptr<m::periodic_timer>
    threadpool::do_create_periodic_timer(std::packaged_task<timer_normal_callable>&& task,
                                         std::wstring                                description)
    {
        return std::make_shared<m::threadpool_impl::linux_impl::timer>(
            m::threadpool_impl::timer::periodic_timer_tag, std::move(task), description);
    }

    std::shared_ptr<m::work_queue>
    threadpool::do_create_work_queue(m::work_queue_execution_policy wqep, std::wstring description)
    {
        return std::make_shared<m::linux_threadpool_impl::work_queue>(wqep, description);
    }
} // namespace m::threadpool_impl

std::shared_ptr<m::threadpool_class>
m::make_platform_default_threadpool()
{
    return std::make_shared<m::threadpool_impl::threadpool>();
}
