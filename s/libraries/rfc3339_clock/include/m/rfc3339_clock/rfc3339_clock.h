// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>
#include <ratio>

#include <time.h>

namespace m
{
    //
    //
    // Why do we have a clock?
    //
    // For representing values in std::chrono that span the entire course of RFC3339 (years 0 -
    // 9999) at millisecond resolution, 48 bits are required.
    //
    // None of the clocks specified in the C++ standard are defined to meet this standard. The
    // MSVC CRT included clocks meet the resolution requirements but have epochs that start later
    // than Year 0. In most cases this wouldn't matter but for parsing arbitrary TOML documents,
    // this means that we either have to have a custom set of types for dealing with dates, times,
    // time offsets, timezones and the like, or a single clock.
    //
    // A clock is not a big deal, we don't need to actually /do/ anything other than define the
    // type that holds the data (int64_t) and the time period (std::chrono::milliseconds).
    //

    class rfc3339_clock
    {
    public:
        using rep                       = int64_t;
        using period                    = std::micro;
        using duration                  = std::chrono::duration<rep, period>;
        using time_point                = std::chrono::time_point<rfc3339_clock, duration>;
        static constexpr bool is_steady = true;

        // Don't use this. This is just to meet the definition of TrivialClock
        static time_point
        now()
        {
            auto now_as_time_t =
                std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            tm now_as_tm{};

#ifdef WIN32
            //
            // Can gmtime_s() really fail? Examples seem to ignore failures.
            // Failures will show up as date/time 0, seems legit. According
            // to the TrivialClock named requirement definition, now() must
            // not fail, so here we are, making a conscious decision to
            // ignore the result.
            //
            std::ignore = gmtime_s(&now_as_tm, &now_as_time_t);
#else
            std::ignore = gmtime_r(&now_as_time_t, &now_as_tm);
#endif

            auto jd = julian_date(static_cast<int64_t>(now_as_tm.tm_year) + 1900ll,
                                  now_as_tm.tm_mon + 1,
                                  now_as_tm.tm_mday);

            auto delta_days = jd - julian_zero;

            //
            // This could all be one expression but this makes it clearer.
            // The optimizer should enregister this as much as it would if
            // the temporaries were not named.
            //
            auto const hours   = static_cast<int64_t>(now_as_tm.tm_hour) + (delta_days * 24ll);
            auto const minutes = static_cast<int64_t>(now_as_tm.tm_min) + (hours * 60ll);
            auto const seconds = static_cast<int64_t>(now_as_tm.tm_sec) + (minutes * 60ll);
            auto const millis  = seconds * 1000ll;
            auto const micros  = millis * 1000ll;

            return time_point(duration(micros));
        }

    private:
        static constexpr int64_t
        julian_date(int64_t y, int64_t m, int64_t d)
        {
#if 0
            m = (m + 9ll) % 12ll;
            y = y - m / 10ll;

            return 365ll * y + y / 4ll - y / 100ll + y / 400ll + (m * 306ll + 5ll) / 10ll +
                   (d - 1ll)
#else
            // From:
            // https://quasar.as.utexas.edu/BillInfo/JulianDatesG.html#:~:text=1%29%20Express%20the%20date%20as%20Y%20M%20D%2C,to%20the%20month%20to%20get%20a%20new%20M.

            // 1) Express the date as Y M D, where Y is the year, M is the month number (Jan = 1,
            // Feb = 2, etc.), and D is the day in the month.

            // 2) If the month is January or February, subtract 1 from the year to get a new Y, and
            // add 12 to the month to get a new M. (Thus, we are thinking of January and February as
            // being the 13th and 14th month of the previous year).
            if ((m == 1) || (m == 2))
            {
                y -= 1;
                m += 12;
            }

            // 3) Dropping the fractional part of all results of all multiplications and divisions,
            // let A = Y / 100
            auto a = y / 100;

            // B = A / 4
            auto b = a / 4;

            // C = 2 - A + B
            auto c = 2 - a + b;

            // E = 365.25x(Y + 4716)
            auto e = 365.25 * static_cast<double>(y + 4716);

            // F = 30.6001x(M + 1)
            auto f = 30.6001 * (m + 1);

            // JD = C + D + E + F - 1524.5
            auto jd = (c + d + e + f) - 1524.5;

            return static_cast<int64_t>(jd);
#endif
        }

        static inline int64_t julian_zero = julian_date(0, 1, 1);
    };
} // namespace m
