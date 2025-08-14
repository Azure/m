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

#include "../../threadpool_timer_impl.h"
#include "threadpool_impl.h"

namespace m::threadpool_impl::linux_impl
{
    class timer : public m::threadpool_impl::timer
    {
        using base_type = m::threadpool_impl::timer;

    public:
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

    protected:
        void
        do_set(duration dur) override;

        void
        do_stop() override;
    };
} // namespace m::threadpool_impl::linux_impl
