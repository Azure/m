// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <tuple>

#include <m/threadpool/threadpool.h>

#include "threadpool_impl.h"
#include "threadpool_timer_impl.h"

std::shared_ptr<m::timer>
m::threadpool_impl::threadpool::do_create_timer(
    std::packaged_task<timer_cancellable_callable>&& task,
    std::wstring&&                                   description)
{
    return std::make_shared<m::threadpool_impl::timer>(
        m::threadpool_impl::timer::task_type(std::move(task)),
        std::forward<std::wstring>(description));
}

std::shared_ptr<m::timer>
m::threadpool_impl::threadpool::do_create_timer(std::packaged_task<timer_callable>&& task,
                                                std::wstring&&                       description)
{
    return std::make_shared<m::threadpool_impl::timer>(
        m::threadpool_impl::timer::task_type(std::move(task)),
        std::forward<std::wstring>(description));
}

std::shared_ptr<m::threadpool_class>
m::make_platform_default_threadpool()
{
    return std::make_shared<m::threadpool_impl::threadpool>();
}
