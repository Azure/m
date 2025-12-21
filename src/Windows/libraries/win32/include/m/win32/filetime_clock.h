// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include <m/utility/compiler.h>

#include <chrono>
#include <ratio>
#include <variant>

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

namespace m::win32
{
    class filetime_clock
    {
    public:
        using rep = std::int64_t;
        using period = std::ratio<1, 10000000>;
        using duration = std::chrono::duration<rep, period>;
        using time_point = std::chrono::time_point<filetime_clock>;
        static constexpr bool is_steady = false;

        static time_point
        now();

        //
        // FILETIME, if positive represents a time. If negative, it represents a
        // duration.
        //
        using from_FILETIME_result = std::variant<time_point, duration>;

        static from_FILETIME_result
        from_sys(FILETIME const& ft);

        static FILETIME
        to_sys(time_point tp);

        static FILETIME
        to_sys(duration dur);
    };

    using filetime_duration = filetime_clock::duration;
    using filetime_time_point = filetime_clock::time_point;
} // namespace m::win32
