// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/tracing/tracing.h>

namespace m::tracing
{
    sink::sink(std::wstring_view name, m::not_null<monitor_class*> monitor):
        m_name(name), m_monitor(monitor), m_closed(false)
    {}
} // namespace m::tracing
