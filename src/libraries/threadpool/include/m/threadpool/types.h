// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <chrono>

namespace m
{
    namespace threadpool_types
    {
        using duration = std::chrono::microseconds;
    }

    using timer_callable             = void();
    using timer_cancellable_callable = void(std::atomic<bool>&);
    using work_item_callable         = void();

} // namespace m
