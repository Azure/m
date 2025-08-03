// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>

#include <m/threadpool/work_queue.h>

#include "work_item_impl.h"

namespace m::threadpool_impl
{
    //
    // Windows FILETIME represents 10ns units
    //
    // using filetime_ratio = std::ratio<1, 100000000>;
    using filetime_ratio = std::ratio<1, 10000000>;

    using file_time = std::chrono::duration<long long, filetime_ratio>;

    class work_queue : public m::work_queue
    {
    public:
        work_queue()                  = default;
        ~work_queue()                 = default;
        work_queue(work_queue const&) = delete;
        work_queue(work_queue&&) noexcept;

        void
        operator=(work_queue const&) = delete;
        work_queue&
        operator=(work_queue&&) noexcept;

        void
        swap(work_queue& other) noexcept;

    private:
        std::shared_ptr<work_item>
        do_enqueue(work_fn&& f) override;

        std::shared_ptr<work_item>
        do_enqueue(work_fn const& f) override;

        std::size_t
        do_queue_size() override;

        std::size_t
        do_running() override;

        bool
        do_wait_for(std::chrono::milliseconds dur) override;

        bool
        do_wait_until(time_point when) override;

        std::mutex                                m_mutex;
        std::condition_variable                   m_cv;
        std::deque<m::threadpool_impl::work_item> m_queue;
    };
} // namespace m::threadpool_impl
