// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <future>
#include <mutex>
#include <optional>

#include <m/chrono/chrono.h>
#include <m/threadpool/work_queue.h>

namespace m::threadpool_impl
{
    class work_executor;

    class work_item : public m::work_item
    {
    public:
        work_item();
        work_item(work_item const&) = delete;
        work_item(work_item&&) noexcept;
        ~work_item() = default;

        work_item&
        operator=(work_item const&) = delete;

        work_item&
        operator=(work_item&&) noexcept;

        void
        swap(work_item& other) noexcept;

    private:
        time_point
        do_enqueue_time() override;

        std::optional<time_point>
        do_start_time() override;

        std::optional<time_point>
        do_end_time() override;

        work_item_times
        do_times() override;

        work_item_state
        do_state() override;

        bool
        do_try_cancel() override;

        bool
        is_done();

        void
        run() noexcept;

        std::mutex                             m_mutex;
        std::condition_variable                m_cv;
        std::packaged_task<work_item_callable> m_packaged_task;
        std::shared_future<void>               m_shared_future;
        m::work_item_times                     m_work_item_times;
        m::work_item_state                     m_state;

        friend class work_executor;
    };
} // namespace m::threadpool_impl
