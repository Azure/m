// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <bit>
#include <chrono>

#include <m/cast/to.h>
#include <m/cast/try_cast.h>
#include <m/chrono/chrono.h>
#include <m/exception/exception.h>
#include <m/tracing/tracing.h>
#include <m/win32/filetime_clock.h>
#include <m/windows_wrappers/win32_dword_ms.h>

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

namespace m
{
    template <typename ResultType, typename Clock, typename Duration = Clock::duration>
        requires clock<Clock>
    struct time_point_cast_helper;

    template <typename ResultType, typename Clock, typename Duration>
    auto
    utc_time_point_cast(std::chrono::time_point<Clock, Duration> const& tp)
    {
        return time_point_cast_helper<ResultType, Clock, Duration>::cast_utc_time_point(tp);
    }

    template <typename Clock>
        requires clock<Clock>
    constexpr auto FILETIME_epoch_lembda =
        [](FILETIME const&, std::chrono::time_point<Clock>& tp) -> void {
            tp = std::chrono::time_point<Clock>(std::chrono::time_point<Clock>::duration::zero());
        };

    //
    // The function signature is overly complex because FILETIME, when
    // positive, represents a time point, and when negative, represents a
    // duration. What to do when it represents a duration is up to the caller.
    //
    // A default on_failure function is provided that sets the output time_point
    // to the epoch ("0"). This is probably what you want, but if you like you could
    // instead throw an exception, or maybe use the current time, or whatever. I
    // considered the various alternatives and none seemed obviously better
    // than the others. I wish this were not true.
    //
    template <class Clock, typename OnFailure>
        requires clock<Clock> &&
                 std::invocable<OnFailure, FILETIME const&, std::chrono::time_point<Clock>&> &&
                 std::same_as<void,
                              std::invoke_result_t<OnFailure,
                                                   FILETIME const&,
                                                   std::chrono::time_point<Clock>&>>
    void
    clock_cast(FILETIME const&                 ft,
               std::chrono::time_point<Clock>& tp,
               OnFailure&&                     on_failure_fn = FILETIME_epoch_lembda<Clock>)
    {
        auto as_ft_tp = m::win32::filetime_clock::from_sys(ft);

        if (std::holds_alternative<m::win32::filetime_clock::time_point>(as_ft_tp))
        {
            auto const ft_tp = std::get<m::win32::filetime_clock::time_point>(as_ft_tp);
            tp               = std::chrono::clock_cast<utc_clock_type>(ft_tp);
        }
        else
        {
            std::invoke(std::forward<OnFailure>(on_failure_fn), ft, tp);
        }
    }

    template <class Clock, typename OnFailure>
        requires clock<Clock> &&
                 std::invocable<OnFailure, FILETIME const&, std::chrono::time_point<Clock>&> &&
                 std::same_as<void,
                              std::invoke_result_t<OnFailure,
                                                   FILETIME const&,
                                                   std::chrono::time_point<Clock>&>>
    auto
    clock_cast(FILETIME const& ft, OnFailure&& on_failure_fn) -> std::chrono::time_point<Clock>
    {
        std::chrono::time_point<Clock> tp;
        clock_cast<Clock>(ft, tp, std::forward<OnFailure>(on_failure_fn));
        return tp;
    }

    template <class Clock>
        requires clock<Clock>
    auto
    clock_cast(FILETIME const& ft) -> std::chrono::time_point<Clock>
    {
        std::chrono::time_point<Clock> tp;
        clock_cast<Clock>(ft, tp, FILETIME_epoch_lembda<Clock>);
        return tp;
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
        cast_utc_time_point(std::chrono::time_point<Clock, Duration> const& tp)
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
        template <typename Rep2, typename Period2>
        static FILETIME
        DurationToFILETIME(std::chrono::duration<Rep2, Period2> const& d)
        {
            if (d.count() < 0)
            {
                trace_error("Programming error: invalid duration passed in; count < 0: {}",
                            d.count());
                throw m::invalid_parameter("duration");
            }

            //
            // FILETIME is 100ns units, so for the ratio
            // have the denominator be 1 billion divided by
            // 100.
            //
            using FiletimeRatio    = std::ratio<1, 1'000'000'000 / 100>;
            using FiletimeDuration = std::chrono::duration<int64_t, FiletimeRatio>;

            auto const as_filetime_duration = std::chrono::duration_cast<FiletimeDuration>(d);

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
        TimePointToFILETIME(utc_time_point_type const& utc_tp)
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

            // Durations in FILETIME are negative.
            //
            return std::bit_cast<FILETIME>(-as_filetime_duration.count());
        }

        static constexpr decltype(auto)
        do_cast(std::chrono::time_point<Clock, Duration> const& tp)
        {
            return TimePointToFILETIME(tp);
        }
    };

} // namespace m
