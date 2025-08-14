// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <chrono>
#include <variant>

#include <m/thread_description/thread_description.h>
#include <m/tracing/tracing.h>

#include "linux_threadpool_timer_impl.h"

using namespace std::chrono_literals;

namespace m::threadpool_impl::linux_impl
{
    timer::timer(normal_timer_tag_t,
                 std::packaged_task<timer_normal_callable>&& task,
                 std::wstring                                description):
        m::threadpool_impl::timer(normal_timer_tag, std::move(task), std::move(description))
    {}

    timer::timer(cancellable_timer_tag_t,
                 std::packaged_task<timer_cancellable_callable>&& task,
                 std::wstring                                     description):
        m::threadpool_impl::timer(cancellable_timer_tag, std::move(task), std::move(description))
    {}

    timer::timer(periodic_timer_tag_t,
                 std::packaged_task<timer_normal_callable>&& task,
                 std::wstring                                description):
        m::threadpool_impl::timer(periodic_timer_tag, std::move(task), std::move(description))
    {}

    timer::~timer() {}

    void
    timer::do_set(duration)
    {
        M_NOT_IMPLEMENTED("Sorry no linux timers");
    }

    void
    timer::do_stop()
    {
        M_NOT_IMPLEMENTED("Sorry no linux timers");
    }

} // namespace m::threadpool_impl::linux_impl
