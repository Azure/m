// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <condition_variable>
#include <deque>
#include <list>
#include <map>
#include <mutex>

#include <m/threadpool/work_item_source.h>
#include <m/threadpool/work_queue.h>
#include <m/threadpool/work_queue_execution_policy.h>

#include "../../work_queue_base.h"

namespace m::threadpool_impl
{
    class work_queue :
        public m::threadpool_impl::work_queue_base,
        public std::enable_shared_from_this<m::threadpool_impl::work_queue>
    {
    public:
        work_queue()                      = default;
        ~work_queue()                     = default;
        work_queue(work_queue const&)     = delete;
        work_queue(work_queue&&) noexcept = delete;
        work_queue(m::work_queue_execution_policy wqep, std::wstring description);

        void
        operator=(work_queue const&) = delete;
        work_queue&
        operator=(work_queue&&) noexcept = delete;

        void
        swap(work_queue& other) noexcept = delete;

    private:
        void
        perform_platform_initialization() override;

        void
        perform_platform_teardown() noexcept override;

        void
        on_new_work_item(std::shared_ptr<m::work_queue_impl::work_item> const& wi) override;
    };
} // namespace m::threadpool_impl
