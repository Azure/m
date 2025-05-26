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
        threadpool(threadpool&&);

    protected:
        std::shared_ptr<m::timer>
        do_create_timer(std::packaged_task<timer_cancellable_callable>&& task) override;

        std::shared_ptr<m::timer>
        do_create_timer(std::packaged_task<timer_callable>&& task) override;
    };
} // namespace m::threadpool_impl
