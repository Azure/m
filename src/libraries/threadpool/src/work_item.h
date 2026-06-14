// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include <m/chrono/chrono.h>
#include <m/error_handling/macros.h>
#include <m/sstring/sstring.h>
#include <m/threadpool/types.h>
#include <m/threadpool/work_item_state.h>
#include <m/utility/pointers.h>

#include <m/threadpool/work_queue.h>

namespace m::work_queue_impl
{
    inline std::atomic<work_item_id_type> work_item_id_counter{1};

    class work_item : public m::work_item
    {
    public:
        work_item()                 = delete;
        work_item(work_item const&) = delete;
        work_item(work_item&&)      = delete;

        work_item(m::wsstring description, std::packaged_task<void()>&& task);

        ~work_item() = default;

        void
        work()
        {
            do_work();
        }

        // Transition a still-queued item to the canceled terminal state and
        // wake any waiters. No-op (returns false) if the item has already
        // started running or already reached a terminal state. Used by queue
        // teardown to release waiters blocked on work that will never start.
        bool
        cancel_if_queued();

    protected:
        utc_time_point_type
        do_enqueue_time() override;

        std::optional<utc_time_point_type>
        do_start_time() override;

        std::optional<utc_time_point_type>
        do_end_time() override;

        work_item_times
        do_times() override;

        work_item_state
        do_state() override;

        m::not_null<m::cwzstring>
        do_description() override;

        bool
        do_try_cancel() override;

        uint64_t
        do_id() override;

        virtual void
        do_work() noexcept;

        void
        do_wait() override;

        bool
        do_wait_for(std::chrono::milliseconds const& d) override;

        bool
        do_wait_until(m::time_point_type const& tp) override;

        work_item_id_type          m_id;          // immutable once constructed
        m::wsstring                m_description; // immutable once constructed
        std::mutex                 m_mutex;
        std::condition_variable    m_state_cv; // signaled when m_work_item_state becomes terminal
        work_item_times            m_work_item_times;
        work_item_state            m_work_item_state;
        std::packaged_task<void()> m_packaged_task;
        std::future<void>          m_future;
    };
} // namespace m::work_queue_impl
