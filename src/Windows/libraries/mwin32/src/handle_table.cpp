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

    handle
    handle_table::intern(std::shared_ptr<m::pil::ikey> const& sp)
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

            auto [it, insertted] = m_table.emplace(std::make_pair(y, data{.m_dv = sp}));
            if (insertted)
                return handle(y);
        }
    }

    void
    handle_table::close(handle h)
    {
        auto l = std::unique_lock(m_mutex);

        auto it = m_table.find(h.m_value);
        if (it == m_table.end())
            m::throw_win32_error_code(ERROR_INVALID_HANDLE);

        m_table.erase(it);
    }

} // namespace m::mwin32_impl