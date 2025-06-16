// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/threadpool/threadpool.h>

namespace m::threadpool_impl
{
    //
    // Windows FILETIME represents 10ns units
    //
    // using filetime_ratio = std::ratio<1, 100000000>;
    using filetime_ratio = std::ratio<1, 10000000>;

    using file_time = std::chrono::duration<long long, filetime_ratio>;

    class threadpool : public m::threadpool_class
    {
    public:
        threadpool() {}
        ~threadpool() {}
        threadpool(threadpool const&) = delete;
        threadpool(threadpool&&);

    protected:
        std::shared_ptr<m::timer>
        do_create_timer(std::packaged_task<timer_cancellable_callable>&& task, std::wstring&& description) override;

        std::shared_ptr<m::timer>
        do_create_timer(std::packaged_task<timer_callable>&& task, std::wstring&& description) override;
    };
} // namespace m::threadpool_impl
