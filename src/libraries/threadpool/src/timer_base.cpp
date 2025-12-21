// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <chrono>
#include <variant>

#include <m/thread_description/thread_description.h>

#include "timer_base.h"

using namespace std::chrono_literals;

namespace m::threadpool_impl
{
    timer_base::timer_base(normal_timer_tag_t,
                           std::packaged_task<timer_callable>&& task,
                           std::wstring                         description):
        m_timer_type(timer_type::normal),
        m_packaged_task(std::move(task)),
        m_description(std::move(description))
    {}

    timer_base::timer_base(periodic_timer_tag_t,
                           std::packaged_task<timer_callable>&& task,
                           std::wstring                         description):
        m_timer_type(timer_type::periodic),
        m_packaged_task(std::move(task)),
        m_description(std::move(description))
    {}

    timer_base::~timer_base() {}

    bool
    timer_base::do_is_set()
    {
        auto l = std::unique_lock(m_mutex);

        return m_set_count != m_set_count_when_executed &&
               m_set_count != m_set_count_when_cancelled;
    }
} // namespace m::threadpool_impl