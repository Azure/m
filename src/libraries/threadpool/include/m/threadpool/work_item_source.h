// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <mutex>
#include <optional>

#include <m/chrono/chrono.h>
#include <m/threadpool/work_queue.h>

namespace m
{
    /// <summary>
    /// A `work_item_source` is a pure virtual abstract class that provides
    /// an interface that may be used to obtain work items.
    ///
    /// There are no provisions for "blocking" to get the next work item,
    /// the tacit assumption is that this is used in the core of an execution
    /// engine and if there are no more work items available from the source,
    /// then none should be returned.
    /// </summary>
    class work_item_source
    {
    public:
        virtual std::optional<std::shared_ptr<m::work_item>>
        try_get_next_work_item() = 0;
    };
} // namespace m
