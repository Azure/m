// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/atomic/atomic.h>
#include <m/tracing/monitor_class.h>

namespace m
{
    namespace tracing
    {
        inline m::atomic_pointer_with_initializer<monitor_class*> monitor;
    } // namespace tracing
} // namespace m
