// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <chrono>
#include <concepts>
#include <ratio>
#include <type_traits>

namespace m
{
    template <typename T>
    concept clock = requires {
        typename T::rep;
        typename T::period;
        typename T::duration;
        typename T::time_point;
        { T::now() } -> std::same_as<typename T::time_point>;
        { T::is_steady } -> std::convertible_to<bool>;
    };

    // Primary trait: false by default
    template <typename T>
    struct is_duration : std::false_type
    {};

    // Specialization for std::chrono::duration
    template <class Rep, class Period>
    struct is_duration<std::chrono::duration<Rep, Period>> : std::true_type
    {};

    // Helper variable template
    template <typename T>
    inline constexpr bool is_duration_v =
        is_duration<std::remove_cv_t<std::remove_reference_t<T>>>::value;

    // Concept form (accepts cv/ref-qualified T)
    template <typename T>
    concept simple_duration = is_duration_v<T>;

    // Optional: a *stricter* duration concept
    //   Ensures Rep is an arithmetic type and Period models a std::ratio.
    //   (This mirrors common expectations when you build generic algorithms.)
    template <typename T>
    concept duration =
        simple_duration<T> &&
        std::is_arithmetic_v<typename std::remove_cv_t<std::remove_reference_t<T>>::rep> &&
        requires {
            // Check that Period looks like a std::ratio (has num/den and can form a ratio type)
            typename std::ratio<std::remove_cv_t<std::remove_reference_t<T>>::period::num,
                                std::remove_cv_t<std::remove_reference_t<T>>::period::den>;
        };

    /// <summary>
    /// Define a standard clock for M
    /// </summary>
    using clock_type = std::chrono::utc_clock;

    /// <summary>
    /// Define a standard time_point for M, based on the standard M clock
    /// </summary>
    using time_point_type = clock_type::time_point;

    template <typename Clock, typename Duration>
    m::time_point_type
    time_point_cast(std::chrono::time_point<Clock, Duration> tp)
    {
        return std::chrono::time_point_cast<m::time_point_type>(tp);
    }

    using utc_clock_type      = std::chrono::utc_clock;
    using utc_time_point_type = std::chrono::utc_clock::time_point;
} // namespace m
