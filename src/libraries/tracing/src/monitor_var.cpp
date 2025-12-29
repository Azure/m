// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <condition_variable>
#include <mutex>

#include <m/tracing/debugging.h>
#include <m/tracing/message.h>

namespace m::tracing
{
    m::not_null<monitor_class*>
    monitor_var::operator->() const noexcept
    {
        return get();
    }

    m::not_null<monitor_class*>
    monitor_var::get() const noexcept
    {
        struct private_state
        {
            private_state(): m_monitor{make_monitor_class()} {}

            m::not_null<monitor_class*>
            get() const
            {
                return m_monitor.get();
            }

            std::unique_ptr<monitor_class> m_monitor;
        };

        static private_state ms_private_state;

        return ms_private_state.get();
    }

    monitor_var::
    operator m::not_null<monitor_class*>() const noexcept
    {
        return get();
    }

    monitor_var monitor;
} // namespace m::tracing
