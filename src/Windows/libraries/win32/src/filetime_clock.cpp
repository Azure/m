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
        SYSTEMTIME st{};
        FILETIME   ft{};
        GetSystemTime(&st);
        SystemTimeToFileTime(&st, &ft);

        LARGE_INTEGER li{};
        li.LowPart = ft.dwLowDateTime;
        li.HighPart = static_cast<LONG>(ft.dwHighDateTime);

        return time_point(duration(li.QuadPart));
    }
} // namespace m::win32