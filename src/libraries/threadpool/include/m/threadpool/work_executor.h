// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <mutex>
#include <optional>

#include <m/chrono/chrono.h>
#include <m/threadpool/work_item_source.h>
#include <m/threadpool/work_queue.h>
#include <m/utility/pointers.h>

namespace m
{
    /// <summary>
    /// A `work_executor` instance represents essentially a thread of
    /// execution which will select work items off a queue for execution.
    ///
    /// A given m::work_queue may have multiple executors associated with it
    /// at any point in time, or may never have more than one associated with
    /// it, depending on its execution policy (in the case that it is set to
    /// sequential vs. parallel execution policy).
    ///
    /// Executors receive their work from "work item providers" in some order,
    /// which is usually the order that they were enqueued, but depending
    /// on scheduling policy, may be any order.
    ///
    /// They generally don't care, they just get work items and run them.
    /// </summary>
    class work_executor
    {
    public:
        virtual void
        execute(m::not_null<work_item_source*> source) noexcept = 0;
    };
} // namespace m
