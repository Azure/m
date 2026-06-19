# Tracing — design notes

## D1. The production monitor is a deliberately leaked process-lifetime singleton

`m::tracing::monitor` (the global `monitor_var`) resolves, on first use, to a single
`monitor_class` instance created by `make_monitor_class()`. That instance is
**intentionally never destroyed** — it is a leaked allocation that lives for the entire
process lifetime. See `monitor_var::get()` in [src/monitor_var.cpp](src/monitor_var.cpp).

### Why

Process-lifetime globals in other components emit trace calls from their destructors
during CRT `atexit` / `DLL_PROCESS_DETACH`. A concrete example: mwin32's global handle
table can still own a directory-watch context (with threadpool timers) at process exit;
tearing those timers down traces. Such a late trace reaches the monitor through a
multiplexor that holds only a **raw back-pointer** to it.

If the monitor were owned by a `static std::unique_ptr` (the previous implementation), it
would be destroyed during static teardown — possibly *before* that last late trace site.
The multiplexor's back-pointer would then dangle and the trace would dereference freed
memory. Depending on what reuses the freed block, this manifested non-deterministically as
**either** an immediate access violation (`0xC0000005`) **or** a hang (control jumping
through a reused vtable slot into code that spins). This was the root cause of the
intermittent Release failure of
`Mwin32NotifySampleLifecycle.ReceivesNotificationsThroughRedirectedPath`.

Leaking the singleton guarantees the monitor outlives every possible trace site for the
whole process, eliminating the dangling back-pointer.

### Teardown coverage is not lost

Leaking the singleton does not reduce coverage of the monitor's construct/use/destroy
cycle. That cycle is exercised on demand through the public `make_monitor_class()` factory,
which builds standalone monitors that are destroyed normally (see
[test/test_monitor_teardown.cpp](test/test_monitor_teardown.cpp)).
