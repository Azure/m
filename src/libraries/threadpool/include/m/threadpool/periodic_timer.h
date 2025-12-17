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
        set()
        {
            return do_set();
        }

        template <typename Rep, typename Period>
        void
        set(std::chrono::duration<Rep, Period> period)
        {
            do_set(std::chrono::duration_cast<duration>(period));
        }

        void
        stop()
        {
            do_stop();
        }

        virtual ~periodic_timer() = default;

    private:
        virtual bool
        do_set() = 0;

        virtual void
        do_set(duration dur) = 0;

        virtual void
        do_stop() = 0;
    };

} // namespace m
