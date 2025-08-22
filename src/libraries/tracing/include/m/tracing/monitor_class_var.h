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
            operator->() const
            {
                return ms_private_state.get();
            }

            m::not_null<monitor_class*>
            get() const
            {
                return ms_private_state.get();
            }

            operator m::not_null<monitor_class*>() const { return ms_private_state; }

        private:
            struct private_state
            {
                private_state(): m_monitor{make_monitor_class()} {}

                m::not_null<monitor_class*>
                operator->() const
                {
                    return m_monitor;
                }

                m::not_null<monitor_class*>
                get() const
                {
                    return m_monitor;
                }

                operator m::not_null<monitor_class*>() const { return m_monitor; }

                m::not_null<monitor_class*> m_monitor;
            };

            inline static private_state ms_private_state;
        };

        inline monitor_class_var monitor;

#if 0
        inline m::atomic_pointer_with_initializer<monitor_class*,
                                                  [] {
                                                      return static_cast<monitor_class*>(
                                                          make_monitor_class());
                                                  }>
            monitor;
#endif
    } // namespace tracing
} // namespace m
