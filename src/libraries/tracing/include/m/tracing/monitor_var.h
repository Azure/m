// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/atomic/atomic.h>
#include <m/tracing/monitor_class.h>
#include <m/utility/pointers.h>

namespace m
{
    namespace tracing
    {
        struct monitor_var
        {
            m::not_null<monitor_class*>
            operator->() const noexcept;

            m::not_null<monitor_class*>
            get() const noexcept;

            operator m::not_null<monitor_class*>() const noexcept;
        };

        extern monitor_var monitor;
    } // namespace tracing
} // namespace m
