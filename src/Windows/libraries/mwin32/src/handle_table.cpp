// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/error_handling/macros.h>
#include <m/errors/errors.h>

#include "handle_table.h"

namespace m::mwin32_impl
{
    HANDLE
    handle::as_HANDLE() const { return reinterpret_cast<HANDLE>(m_value); }

    HKEY
    handle::as_HKEY() const
    {
        return reinterpret_cast<HKEY>(m_value);
    }

    handle
    handle::from_HANDLE(HANDLE h)
    {
        handle hdl;
        hdl.m_value = reinterpret_cast<uintptr_t>(h);
        return hdl;
    }

    handle
    handle::from_HKEY(HKEY hkey)
    {
        handle hdl;
        hdl.m_value = reinterpret_cast<uintptr_t>(hkey);
        return hdl;
    }

    handle_table::handle_table(): m_mt{m_rd()}, m_random_mask{m_mt()}, m_counter{} {}

    namespace
    {
        //
        // Armed exactly once, by DllMain on DLL_PROCESS_DETACH, when the process
        // is terminating (lpReserved != nullptr). The loader calls
        // DllMain(DLL_PROCESS_DETACH) before the CRT runs the static-destructor /
        // atexit table that owns g_handles, so ~handle_table observes the final
        // value. By the time it is set the OS has already terminated every other
        // thread, so a plain bool needs no synchronization.
        //
        bool g_process_terminating = false;
    } // namespace

    handle_table::~handle_table()
    {
        if (!g_process_terminating)
            return;

        //
        // Process rundown. Per Microsoft's DLL_PROCESS_DETACH guidance, do no
        // cleanup when the process is terminating: every other thread is already
        // gone, so releasing a payload here can tear down a directory-watch
        // context whose timer/wait destructors (a) block forever in
        // WaitForThreadpool*Callbacks waiting on worker threads the OS has
        // destroyed and (b) trace through late-shutdown infrastructure. Both were
        // observed as the intermittent teardown hang / access violation.
        //
        // Deliberately leak the table by moving it into a heap allocation that is
        // never freed, so the contained shared_ptr<file_handle_state>/... payloads
        // are never released. The OS reclaims the address space on exit. This
        // path runs only at process termination; a FreeLibrary unload (where the
        // process lives on) leaves g_process_terminating false and runs normal
        // teardown.
        //
        new std::map<uintptr_t, data>(std::move(m_table));
    }

    handle
    handle_table::intern_variant(data_variant_type dv)
    {
        auto l = std::unique_lock(m_mutex);

        for (;;)
        {
            uintptr_t x = m_counter++;
            x ^= m_random_mask;

            constexpr uintptr_t mask = (1ull << 27) - 1ull;

            x &= mask;

            uintptr_t y = (x << 2) | (1ull << 30);

            //
            // y is the (proposed) handle value. now see if it's already in the handle
            // table. hard to believe that we've actually wrapped 2^27 but still we will
            // keep incrementing.
            //

            auto [it, insertted] = m_table.emplace(std::make_pair(y, data{.m_dv = std::move(dv)}));
            if (insertted)
                return handle(y);
        }
    }

    handle
    handle_table::intern(std::shared_ptr<m::pil::ikey> const& sp)
    {
        return intern_variant(data_variant_type{sp});
    }

    handle
    handle_table::intern(std::shared_ptr<file_handle_state> const& sp)
    {
        return intern_variant(data_variant_type{sp});
    }

    handle
    handle_table::intern(std::shared_ptr<find_enumeration_state> const& sp)
    {
        return intern_variant(data_variant_type{sp});
    }

    handle
    handle_table::intern(std::shared_ptr<stream_enumeration_state> const& sp)
    {
        return intern_variant(data_variant_type{sp});
    }

    void
    handle_table::close(handle h)
    {
        //
        // Predefined registry pseudo-handles (HKEY_LOCAL_MACHINE, ...) are
        // always-open and were never interned; closing one is a success no-op,
        // matching Win32 RegCloseKey semantics.
        //
        if (is_predefined_handle_value(h.m_value))
            return;

        auto l = std::unique_lock(m_mutex);

        auto it = m_table.find(h.m_value);
        if (it == m_table.end())
            m::throw_win32_error_code(ERROR_INVALID_HANDLE);

        m_table.erase(it);
    }

    bool
    handle_table::is_minted_handle_value(handle h) noexcept
    {
        uintptr_t const v = h.m_value;

        // The reserved encoding (see handle_table.h): bit 30 set, bit 29 clear,
        // bits 0-1 clear, and nothing at or above bit 31.
        constexpr uintptr_t bit30        = 1ull << 30;
        constexpr uintptr_t bit29        = 1ull << 29;
        constexpr uintptr_t low_two_bits = 0x3ull;
        constexpr unsigned  high_shift   = 31;

        if ((v >> high_shift) != 0)
            return false;
        if ((v & bit30) == 0)
            return false;
        if ((v & bit29) != 0)
            return false;
        if ((v & low_two_bits) != 0)
            return false;

        return true;
    }

} // namespace m::mwin32_impl

//
// mwin32's process-rundown hook. The C runtime's _DllMainCRTStartup calls this
// DllMain on DLL_PROCESS_DETACH *before* it runs the static-destructor / atexit
// table (which includes g_handles). lpReserved distinguishes the two detach
// causes: non-null means the process is terminating; null means a FreeLibrary
// unload while the process keeps running.
//
// On process termination we arm the rundown flag so handle_table::~handle_table
// leaks rather than tearing down live directory watches (see the destructor
// above and mwin32 DESIGN-NOTES.md). Only a trivial store is done here: DllMain
// runs under the loader lock, where waiting on threadpool callbacks, allocating,
// or tracing would risk deadlock. The FreeLibrary case (lpReserved == nullptr)
// is left to run normal teardown; a DLL client that unloads mwin32 mid-process
// must quiesce outstanding watches via the redirecting-library rundown helper
// before calling FreeLibrary.
//
BOOL WINAPI
DllMain(HINSTANCE /*instance*/, DWORD reason, LPVOID reserved)
{
    if ((reason == DLL_PROCESS_DETACH) && (reserved != nullptr))
        m::mwin32_impl::g_process_terminating = true;

    return TRUE;
}
