// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/threadpool/threadpool.h>

namespace m::threadpool_impl
{
    class threadpool : public m::threadpool_class
    {
    public:
        threadpool() {}
        ~threadpool() {}
        threadpool(threadpool const&) = delete;
        threadpool(threadpool&&) noexcept;
        void
        operator=(threadpool const&) = delete;
        threadpool&
        operator=(threadpool&&) noexcept;

        void
        swap(threadpool& other) noexcept;

    protected:
        std::unique_ptr<m::timer>
        do_create_timer(std::packaged_task<timer_callable>&& task) override;

        std::unique_ptr<m::timer>
        do_create_timer(std::packaged_task<timer_callable>&& task,
                        std::wstring                                description) override;

        std::unique_ptr<m::periodic_timer>
        do_create_periodic_timer(std::packaged_task<timer_callable>&& task) override;

        std::unique_ptr<m::periodic_timer>
        do_create_periodic_timer(std::packaged_task<timer_callable>&& task,
                                 std::wstring                                description) override;

        std::shared_ptr<m::work_queue>
        do_create_work_queue(m::work_queue_execution_policy wqep,
                             std::wstring                   description) override;
    };
} // namespace m::threadpool_impl
