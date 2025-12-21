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
        struct monitor_class_var
        {
            m::not_null<monitor_class*>
            inline operator->() const noexcept
            {
                return get();
            }

            m::not_null<monitor_class*>
            inline get() const noexcept
            {
                return static_get();
            }

            inline operator m::not_null<monitor_class*>() const noexcept { return get(); }

            private:
            static m::not_null<monitor_class*>
            static_get() noexcept;
        };

        extern monitor_class_var monitor;
    } // namespace tracing
} // namespace m
