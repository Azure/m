// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "rundown.h"

#if defined(_WIN32)
#undef NOMINMAX
#define NOMINMAX
#include <Windows.h>
#endif

namespace m::pil::impl::redirecting
{
#if defined(_WIN32)
    bool
    process_rundown_in_progress() noexcept
    {
        //
        // RtlDllShutdownInProgress is exported by ntdll but is not declared in the
        // public SDK headers, so resolve it once by name. ntdll is mapped into
        // every Windows process, so GetModuleHandleW never has to load it. A
        // failure to resolve the export is treated as "not in rundown" -- the
        // conservative answer that keeps normal teardown running.
        //
        using shutdown_in_progress_fn = BOOLEAN(NTAPI*)();

        static shutdown_in_progress_fn const fn = []() noexcept -> shutdown_in_progress_fn {
            if (HMODULE const ntdll = ::GetModuleHandleW(L"ntdll.dll"))
                return reinterpret_cast<shutdown_in_progress_fn>(
                    reinterpret_cast<void*>(::GetProcAddress(ntdll, "RtlDllShutdownInProgress")));

            return nullptr;
        }();

        return (fn != nullptr) && (fn() != FALSE);
    }
#else
    bool
    process_rundown_in_progress() noexcept
    {
        return false;
    }
#endif
} // namespace m::pil::impl::redirecting
