// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <chrono>
#include <variant>

#include <m/thread_description/thread_description.h>
#include <m/tracing/tracing.h>

#include "timer.h"

using namespace std::chrono_literals;

namespace m::threadpool_impl
{
    timer::timer(normal_timer_tag_t,
                 std::packaged_task<timer_callable>&& task,
                 std::wstring                         description):
        timer_base(normal_timer_tag, std::move(task), std::move(description))
    {}

    timer::timer(periodic_timer_tag_t,
                 std::packaged_task<timer_callable>&& task,
                 std::wstring                         description):
        timer_base(periodic_timer_tag, std::move(task), std::move(description))
    {}

    timer::~timer() {}

    void
    timer::do_set(duration)
    {
        M_NOT_IMPLEMENTED("Sorry no linux timers");
    }

    void
    timer::do_cancel()
    {
        M_NOT_IMPLEMENTED("Sorry no linux timers");
    }

    void
    timer::do_stop()
    {
        M_NOT_IMPLEMENTED("Sorry no linux timers");
    }

    void
    timer::do_wait()
    {
        M_NOT_IMPLEMENTED("Sorry no linux timers");
    }

} // namespace m::threadpool_impl
