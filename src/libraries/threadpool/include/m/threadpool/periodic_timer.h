// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>

#include <m/chrono/chrono.h>
#include <m/threadpool/types.h>
#include <m/utility/pointers.h>
#include <m/utility/quantum_types.h>

namespace m
{
    class periodic_timer
    {
    public:
        /// <summary>
        /// Returns whether the timer is `set`.
        /// </summary>
        /// <returns></returns>
        bool
        is_set()
        {
            return do_is_set();
        }

        template <typename Rep, typename Period>
        void
        set(std::chrono::duration<Rep, Period> period)
        {
            do_set(m::duration_cast(period));
        }

        void
        stop()
        {
            do_stop();
        }

        /// <summary>
        /// Waits for the timer to cease to function. Undefined behavior if
        /// this->is_set() is true.
        /// </summary>
        /// <returns></returns>
        void
        wait()
        {
            do_wait();
        }

        virtual ~periodic_timer() = default;

    private:
        virtual bool
        do_is_set() = 0;

        virtual void
        do_set(duration_type const& dur) = 0;

        virtual void
        do_stop() = 0;

        virtual void
        do_wait() = 0;
    };

} // namespace m
