// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <utility>
#include <variant>
#include <vector>

#include <m/threadpool/threadpool.h>
#include <m/utility/pointers.h>

namespace m::threadpool_impl
{
    class timer : public m::timer, public m::periodic_timer
    {
    public:
        struct normal_timer_tag_t
        {
            explicit normal_timer_tag_t() = default;
        };

        inline static normal_timer_tag_t normal_timer_tag;

        struct cancellable_timer_tag_t
        {
            explicit cancellable_timer_tag_t() = default;
        };

        inline static cancellable_timer_tag_t cancellable_timer_tag;

        struct periodic_timer_tag_t
        {
            explicit periodic_timer_tag_t() = default;
        };

        inline static periodic_timer_tag_t periodic_timer_tag;

    protected:
        timer(normal_timer_tag_t,
              std::packaged_task<timer_normal_callable>&& task,
              std::wstring                                description);

        timer(cancellable_timer_tag_t,
              std::packaged_task<timer_cancellable_callable>&& task,
              std::wstring                                     description);

        timer(periodic_timer_tag_t,
              std::packaged_task<timer_normal_callable>&& task,
              std::wstring                                description);

        timer(m::threadpool_impl::timer&& other) = delete;
        timer(m::threadpool_impl::timer const&)  = delete;
        ~timer();

        void
        operator=(m::threadpool_impl::timer const&) = delete;

        m::threadpool_impl::timer&
        operator=(m::threadpool_impl::timer&& other) = delete;

        bool
        do_cancel_requested() override;

        bool
        do_done() override;

        void
        do_try_cancel() override;

        bool
        do_set() override;

        /// <summary>
        /// The `do_set` does not have to be called out here at all but is so
        /// that it's clear that this still needs to be overridden by
        /// a derived class for this abstract class to become cooncrete.
        /// </summary>
        /// <param name="dur"></param>
        virtual void
        do_set(duration dur) override = 0;

        virtual void
        do_stop() override = 0;

        enum class task_type
        {
            normal,
            cancellable,
            periodic,
        };

        mutable std::mutex                             m_mutex;
        task_type                                      m_task_type;
        std::packaged_task<timer_normal_callable>      m_normal_packaged_task;
        std::packaged_task<timer_cancellable_callable> m_cancellable_packaged_task;
        duration                                       m_duration;
        std::wstring                                   m_description;
        std::atomic<bool>                              m_cancel_requested{false};
        std::atomic<bool>                              m_done{true};
        bool                                           m_cancelled{false};
        bool                                           m_started{false};
    };

} // namespace m::threadpool_impl
