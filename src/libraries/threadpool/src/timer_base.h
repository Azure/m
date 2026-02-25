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
    class timer_base : public m::timer, public m::periodic_timer
    {
    public:
        struct normal_timer_tag_t
        {
            normal_timer_tag_t() = default;
        };

        inline static normal_timer_tag_t normal_timer_tag;

        struct periodic_timer_tag_t
        {
            periodic_timer_tag_t() = default;
        };

        inline static periodic_timer_tag_t periodic_timer_tag;

    protected:
        timer_base(normal_timer_tag_t,
                   std::packaged_task<timer_callable>&& task,
                   std::wstring                         description);

        timer_base(periodic_timer_tag_t,
                   std::packaged_task<timer_callable>&& task,
                   std::wstring                         description);

        timer_base(m::threadpool_impl::timer_base&& other) = delete;
        timer_base(m::threadpool_impl::timer_base const&)  = delete;
        ~timer_base();

        void
        operator=(m::threadpool_impl::timer_base const&) = delete;

        m::threadpool_impl::timer_base&
        operator=(m::threadpool_impl::timer_base&& other) = delete;

        bool
        do_is_set() override;

        /// <summary>
        /// The `do_set` does not have to be called out here at all but is so
        /// that it's clear that this still needs to be overridden by
        /// a derived class for this abstract class to become cooncrete.
        /// </summary>
        /// <param name="dur"></param>
        virtual void
        do_set(duration_type const& dur) override = 0;

        virtual void
        do_stop() override = 0;

        virtual void
        do_wait() override = 0;

        enum class timer_type
        {
            normal,
            periodic,
        };

        //
        // Instead of maintaining some careful state diagram about "started",
        // "cancelling", "cancelled", etc. states, I believe that we can derive
        // the state of the timer from simpler to maintain pieces of data.
        //
        // Instead we will maintain a counter that is incremented each time
        // the timer is set in m_set_count.
        //
        // When the timer fires, m_set_count_when_executed is set to that
        // same value.
        //
        // For non-periodic timers, this gives a simple way to derive whether
        // the timer has been set or not: m_set_count == m_set_count_when_executed.
        //
        // When equal, the timer is not set, when not equal, the timer is set.
        //
        // At least is starts off easy. Then we add in cancellation. Cancellation
        // only takes the m_set_count at the time of cancellation and records it
        // in m_set_count_when_cancelled.
        //
        // Now the states are:
        //
        // Not set: m_set_count == m_set_count_when_executed
        // set, not cancelled: (m_set_count != m_set_count_when_executed) &&
        //                     (m_set_count != m_set_count_when_cancelled)
        // set, cancelled: (m_set_count != m_set_count_when_executed) &&
        //                 (m_set_count == m_set_count_when_cancelled)
        //
        // If the timer is not set, you can't tell if it's "cancelled", that's
        // nonsense.
        //
        // The remainder issue is "finalization" which is a strange way to state
        // that there may be a need to await the completion of the execution of the
        // packaged task. This arises because cancellation _requests_ cancellation
        // but the underlying mechanisms used may not be able to actually cancel
        // the dispatch in time. This is what the "wait*()" member functions are
        // all about.
        //
        // For non-periodic timers, this is almost certainly easy, although the
        // way that non-periodic timers are "cleaned up" on Windows is somewhat
        // scary and seems to imply that they may be fired one extra time.
        //
        // For periodic timers, this is almost certainly trickier, since the
        // cancellation occurs, and it's unclear how many extra times the timer
        // may be dispatched before we are done. Windows provides an API for
        // synchronizing with the underlying timer object to be finished with
        // its dispatch, and the Linux implementation is TBD so we should just
        // be smart when we do it.
        //
        // It's unfortunate that this is a lot of space and perhaps it should be
        // condensed down to something smaller but we probably want to keep
        // counters for these things anyhow.
        //

        mutable std::mutex                 m_mutex;
        timer_type                         m_timer_type;
        std::packaged_task<timer_callable> m_packaged_task;
        duration_type                      m_duration;
        std::wstring                       m_description;
        std::uintmax_t                     m_set_count{};
        std::uintmax_t                     m_set_count_when_cancelled{};
        std::uintmax_t                     m_set_count_when_executed{};
        std::uintmax_t                     m_set_count_when_finalized{};
        std::uintmax_t                     m_re_execution_count{};
    };

} // namespace m::threadpool_impl
