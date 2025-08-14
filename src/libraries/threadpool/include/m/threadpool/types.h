// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <chrono>

#include <m/utility/quantum_types.h>

namespace m
{
    using timer_normal_callable      = void();
    using timer_cancellable_callable = void(std::atomic<bool>&);
    using work_item_callable         = void();

} // namespace m
