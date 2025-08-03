// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <string_view>
#include <utility>

#include <m/tracing/monitor_class.h>
#include <m/tracing/source.h>
#include <m/tracing/tracing.h>
#include <m/utility/compiler.h>

namespace m
{
    M_NOINLINE
    void
    trace_internal_error_check_failure(std::source_location const& srcloc, m::czstring expression)
    {
        trace(m::tracing::event_kind::critical,
              "!!! Failed internal error check !!! Expression: \"{}\" at {}({})",
              expression,
              srcloc.file_name(),
              srcloc.line());
    }
} // namespace m