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
        using timer_duration = typename threadpool_types::duration;

    public:
        bool
        cancel_requested()
        {
            return do_cancel_requested();
        }

        bool
        done()
        {
            return do_done();
        }

        void
        try_cancel()
        {
            do_try_cancel();
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
            do_set(std::chrono::duration_cast<timer_duration>(dur));
        }

    private:
        virtual ~timer() {}

        virtual bool
        do_cancel_requested() = 0;

        virtual bool
        do_done() = 0;

        virtual void
        do_try_cancel() = 0;

        virtual void
        do_set(timer_duration dur) = 0;
    };

} // namespace m
