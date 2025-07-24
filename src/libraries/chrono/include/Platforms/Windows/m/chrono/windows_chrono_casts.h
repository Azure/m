// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <chrono>

#include <m/cast/to.h>
#include <m/cast/try_cast.h>
#include <m/exception/exception.h>

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

namespace m
{
    template <typename ResultType, typename Clock, typename Duration = Clock::duration>
    struct time_point_cast_helper;

    template <typename ResultType, typename Clock, typename Duration>
    auto
    utc_time_point_cast(std::chrono::time_point<Clock, Duration> tp)
    {
        return time_point_cast_helper<ResultType, Clock, Duration>::cast_utc_time_point(tp);
    }

    template <typename Clock, typename Duration>
    struct time_point_cast_helper<SYSTEMTIME, Clock, Duration>
    {

        template <typename DestinationClock>
        static SYSTEMTIME
        cast_time_point(std::chrono::time_point<Clock, Duration> tp)
        {
            auto const as_destination_clock = std::chrono::clock_cast<DestinationClock>(tp);
            auto const sys_time             = std::chrono::utc_clock::to_sys(as_destination_clock);
            auto const time_t_time          = std::chrono::system_clock::to_time_t(sys_time);
            std::tm    tm_time{};

            gmtime_s(&tm_time, &time_t_time);

            SYSTEMTIME st{};

            st.wDay          = m::to<WORD>(tm_time.tm_mday);
            st.wDayOfWeek    = m::to<WORD>(tm_time.tm_wday);
            st.wHour         = m::to<WORD>(tm_time.tm_hour);
            st.wMilliseconds = 0;
            st.wMinute       = m::to<WORD>(tm_time.tm_min);
            st.wMonth        = m::to<WORD>(tm_time.tm_mon) + 1;
            st.wSecond       = m::to<WORD>(tm_time.tm_sec);
            st.wYear         = m::to<WORD>(tm_time.tm_year) + 1900;

            return st;
        }

        static SYSTEMTIME
        cast_utc_time_point(std::chrono::time_point<Clock, Duration> tp)
        {
            return cast_time_point<std::chrono::utc_clock>(tp);
        }
    };

} // namespace m
