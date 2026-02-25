// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/utility/compiler.h>

#include <m/error_handling/macros.h>
#include <m/errors/errors.h>
#include <m/win32/filetime_clock.h>

namespace m::win32
{
    filetime_clock::time_point
    filetime_clock::now()
    {
        FILETIME ft{};
        ::GetSystemTimeAsFileTime(&ft);

        LARGE_INTEGER li{};
        li.LowPart  = ft.dwLowDateTime;
        li.HighPart = static_cast<LONG>(ft.dwHighDateTime);

        return time_point(duration(li.QuadPart));
    }

    filetime_clock::from_FILETIME_result
    filetime_clock::from_sys(FILETIME const& ft)
    {
        LARGE_INTEGER li{};
        li.LowPart  = ft.dwLowDateTime;
        li.HighPart = static_cast<decltype(li.HighPart)>(ft.dwHighDateTime);

        if (li.HighPart < 0)
        {
            return duration(-li.QuadPart);
        }

        return time_point(duration(li.QuadPart));
    }

    FILETIME
    filetime_clock::to_sys(time_point const& tp)
    {
        LARGE_INTEGER li{};
        li.QuadPart = tp.time_since_epoch().count();
        FILETIME ft{};
        ft.dwLowDateTime  = li.LowPart;
        ft.dwHighDateTime = static_cast<decltype(ft.dwHighDateTime)>(li.HighPart);
        return ft;
    }

    FILETIME
    filetime_clock::to_sys(duration const& dur)
    {
        LARGE_INTEGER li{};
        li.QuadPart = -dur.count();
        FILETIME ft{};
        ft.dwLowDateTime  = li.LowPart;
        ft.dwHighDateTime = static_cast<decltype(ft.dwHighDateTime)>(li.HighPart);
        return ft;
    }

} // namespace m::win32
