// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <bit>
#include <chrono>

#include <m/cast/to.h>
#include <m/cast/try_cast.h>
#include <m/exception/exception.h>
#include <m/tracing/tracing.h>
#include <m/windows_wrappers/win32_dword_ms.h>

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

    template <typename Rep, typename Period>
    win32_dword_ms
    win32_dword_ms_cast(std::chrono::duration<Rep, Period> const& d)
    {
        auto const as_ms = std::chrono::duration_cast<std::chrono::milliseconds>(d);

        using value_type = typename win32_dword_ms::value_type;

        if (as_ms.count() < (std::numeric_limits<value_type>::min)() ||
            as_ms.count() > (std::numeric_limits<value_type>::max)())
        {
            m::trace_error("Attempted to convert value {} to DWORD milliseconds; out of range",
                           as_ms.count());
            throw m::invalid_parameter("d");
        }

        return win32_dword_ms(m::to<DWORD>(as_ms.count()));
    }

    template <typename Rep, typename Period>
        requires(std::integral<Rep>)
    struct try_cast_helper<std::chrono::duration<Rep, Period>, win32_dword_ms, void>
    {
        static constexpr decltype(auto)
        do_cast(std::chrono::duration<Rep, Period> const& d)
        {
            return win32_dword_ms_cast(d);
        }
    };

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

    template <typename Clock, typename Duration>
    struct try_cast_helper<std::chrono::time_point<Clock, Duration>, SYSTEMTIME, void>
    {
        static constexpr decltype(auto)
        do_cast(std::chrono::time_point<Clock, Duration> const& tp)
        {
            return time_point_cast_helper<SYSTEMTIME, Clock, Duration>::cast_utc_time_point(tp);
        }
    };

    template <typename Rep, typename Period>
    struct try_cast_helper<std::chrono::duration<Rep, Period>, FILETIME, void>
    {
        template <typename Rep, typename Period>
        static FILETIME
        DurationToFILETIME(std::chrono::duration<Rep, Period> const& duration)
        {
            if (duration.count() < 0)
            {
                trace_error("Programming error: invalid duration passed in; count < 0: {}",
                            duration.count());
                throw m::invalid_parameter("duration");
            }

            //
            // FILETIME is 100ns units, so for the ratio
            // have the denominator be 1 billion divided by
            // 100.
            //
            using FiletimeRatio    = std::ratio<1, 1'000'000'000 / 100>;
            using FiletimeDuration = std::chrono::duration<int64_t, FiletimeRatio>;

            auto const as_filetime_duration =
                std::chrono::duration_cast<FiletimeDuration>(duration);

            static_assert(std::numeric_limits<int64_t>::digits ==
                          std::numeric_limits<typename FiletimeDuration::rep>::digits);

            int64_t count = as_filetime_duration.count();

            // Durations in FILETIME are negative.
            //
            count = -count;

            return std::bit_cast<FILETIME>(count);
        }

        static constexpr decltype(auto)
        do_cast(std::chrono::duration<Rep, Period> const& d)
        {
            return DurationToFILETIME(d);
        }
    };

    template <typename Clock, typename Duration>
    struct try_cast_helper<std::chrono::time_point<Clock, Duration>, FILETIME, void>
    {
        static FILETIME
        TimePointToFILETIME(utc_time_point utc_tp)
        {
            auto const duration = utc_tp.time_since_epoch();

            //
            // FILETIME is 100ns units, so for the ratio
            // have the denominator be 1 billion divided by
            // 100.
            //
            using FiletimeRatio    = std::ratio<1, 1'000'000'000 / 100>;
            using FiletimeDuration = std::chrono::duration<int64_t, FiletimeRatio>;

            auto const as_filetime_duration =
                std::chrono::duration_cast<FiletimeDuration>(duration);

            static_assert(std::numeric_limits<int64_t>::digits ==
                          std::numeric_limits<typename FiletimeDuration::rep>::digits);

            int64_t count = as_filetime_duration.count();

            // Durations in FILETIME are negative.
            //
            count = -count;

            return std::bit_cast<FILETIME>(count);
        }

        static constexpr decltype(auto)
        do_cast(std::chrono::time_point<Clock, Duration> const& tp)
        {
            return TimePointToFILETIME(tp);
        }
    };

} // namespace m
