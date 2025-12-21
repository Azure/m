// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>

#include <m/threadpool/types.h>
#include <m/utility/pointers.h>

namespace m
{
    class timer
    {
    public:
        bool
        is_set()
        {
            return do_is_set();
        }

        /// <summary>
        /// The `set` member function is only valid on a timer that is
        /// in the `done` state. Attempting to execute it on a timer that
        /// is in any other state results in undefined behavior.
        ///
        /// Timers begin their existence in the "done" state.
        /// </summary>
        /// <typeparam name="Rep"></typeparam>
        /// <typeparam name="Period"></typeparam>
        /// <param name="dur"></param>
        template <typename Rep, typename Period>
        void
        set(std::chrono::duration<Rep, Period> dur)
        {
            do_set(m::duration_cast(dur));
        }

        void
        set()
        {
            do_set(m::duration(0));
        }

        void
        cancel()
        {
            do_cancel();
        }

        void
        wait()
        {
            do_wait();
        }

        virtual ~timer() = default;

    private:
        virtual bool
        do_is_set() = 0;

        virtual void
        do_set(duration dur) = 0;

        virtual void
        do_cancel() = 0;

        virtual void
        do_wait() = 0;
    };

} // namespace m
