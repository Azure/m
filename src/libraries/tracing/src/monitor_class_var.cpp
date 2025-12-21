// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <condition_variable>
#include <mutex>

#include <m/tracing/debugging.h>
#include <m/tracing/message.h>
#include <m/tracing/monitor_class.h>

#include "monitor_class_impl.h"

namespace m::tracing
{
    m::not_null<monitor_class*>
    monitor_class_var::static_get() noexcept
    {
        struct magic_static
        {
            magic_static(): m_monitor_class(std::make_unique<tracing_impl::monitor>()) {}

            m::not_null<monitor_class*>
            get() noexcept
            {
                return m_monitor_class.get();
            }

            std::unique_ptr<monitor_class> m_monitor_class;
        };

        static magic_static ms_the_monitor;

        return ms_the_monitor.get();
    }

    monitor_class_var monitor;
} // namespace m::tracing
