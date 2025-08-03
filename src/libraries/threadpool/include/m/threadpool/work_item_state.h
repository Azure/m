// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <chrono>

namespace m
{
    enum class work_item_state
    {
        queued,
        running,
        done,
        canceled,
    };
} // namespace m
