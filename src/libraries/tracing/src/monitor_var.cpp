// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <condition_variable>
#include <mutex>

#include <m/tracing/debugging.h>
#include <m/tracing/message.h>
#include <m/tracing/monitor_var.h>

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
        //
        // The production tracing monitor is a process-lifetime singleton that is
        // intentionally never destroyed (a deliberately leaked allocation).
        //
        // Other process-lifetime globals emit trace calls from their destructors
        // during the CRT atexit / DLL_PROCESS_DETACH phase. A concrete example is
        // mwin32's global handle table, which can still own a directory-watch
        // context (with threadpool timers) at process exit; tearing those timers
        // down traces. Such a trace routes through this monitor by way of a
        // multiplexor that holds only a raw back-pointer to it. If the monitor
        // were owned by a static unique_ptr it would be destroyed during static
        // teardown, possibly before that last late user, leaving the back-pointer
        // dangling and the trace call dereferencing freed memory. Depending on
        // what reuses the freed block, that manifests either as an immediate
        // access violation or as a hang (control jumps through a reused vtable
        // slot into code that spins). Leaking the monitor guarantees it outlives
        // every possible trace site for the entire process lifetime.
        //
        // This does not reduce teardown coverage: the construct/use/destroy cycle
        // is exercised on demand through the public make_monitor_class() factory
        // (see test_monitor_teardown.cpp), which builds standalone monitors rather
        // than this singleton.
        //
        static m::not_null<monitor_class*> const the_monitor =
            make_monitor_class().release();

        return the_monitor;
    }

    monitor_var::
    operator m::not_null<monitor_class*>() const noexcept
    {
        return get();
    }

    monitor_var monitor;
} // namespace m::tracing
