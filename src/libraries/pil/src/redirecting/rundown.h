// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

namespace m::pil::impl::redirecting
{
    //
    // Reports whether the host process has entered loader-driven rundown: the
    // operating-system loader has begun tearing the process down (the
    // DLL_PROCESS_DETACH that accompanies process termination), as opposed to a
    // FreeLibrary unload while the process keeps running.
    //
    // Teardown paths that would otherwise block on threadpool callbacks (for
    // example a directory-watch token whose destructor calls
    // WaitForThreadpool*Callbacks) must consult this first. During process
    // rundown the OS has already terminated every other thread, so the worker
    // threads those callbacks would run on are gone and the wait would hang
    // forever; the caller should skip the wait and leak instead, letting the OS
    // reclaim the address space.
    //
    // This is true *only* during process termination. A single FreeLibrary unload
    // (the process is still alive) reports false, so normal teardown runs -- which
    // is why a DLL client that unloads its provider mid-process must quiesce
    // outstanding watches itself before calling FreeLibrary rather than relying on
    // this signal.
    //
    // On Windows this is backed by ntdll's RtlDllShutdownInProgress. On platforms
    // without that concept it reports false (teardown always proceeds normally).
    //
    bool
    process_rundown_in_progress() noexcept;
} // namespace m::pil::impl::redirecting
