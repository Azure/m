// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include <m/utility/compiler.h>

#include <chrono>
#include <ratio>
#include <variant>

#include <m/chrono/chrono.h>

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

namespace m::win32
{
    class filetime_clock
    {
    public:
        using rep                       = std::int64_t;
        using period                    = std::ratio<1, 10000000>;
        using duration                  = std::chrono::duration<rep, period>;
        using time_point                = std::chrono::time_point<filetime_clock>;
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
        to_sys(time_point const& tp);

        static FILETIME
        to_sys(duration const& dur);

        template <typename Duration>
        using filetime_time = std::chrono::time_point<filetime_clock, Duration>;

        // This whole thing is kind of ... weird. The whole point of the filetime
        // clock is to use the period that FILETIME uses. So varying the Duration
        // type implies that the caller is using varying ... rep types?
        //
        // Why would they do this? But the function seems to be required in order
        // to mesh with the time_point_cast_helper stuff in the std <chrono>
        // header.
        template <typename Duration>
        static std::chrono::utc_clock::time_point
        to_utc(filetime_time<Duration> const& t)
        {
            using utcish_duration_type = std::chrono::duration<intmax_t, utc_clock_type::period>;
            auto d = std::chrono::duration_cast<utcish_duration_type>(t.time_since_epoch());

            //
            // Now the tricky part, the epoch conversion.
            //
            // The value here was determined entirely experimentally. If it is incorrect, please
            // fix.
            //

            constexpr int64_t epoch_difference_in_100ns_units =
                // 116444736000000000ll; // 1970 - 1601 in 100ns units [micgrier: CoPilot suggested
                // this, let's see!]
                116444736000000000; // Grok suggests this
            d -= utcish_duration_type{epoch_difference_in_100ns_units};

            return std::chrono::utc_clock::time_point(d);
        }
    };

    using filetime_duration   = filetime_clock::duration;
    using filetime_time_point = filetime_clock::time_point;
} // namespace m::win32
