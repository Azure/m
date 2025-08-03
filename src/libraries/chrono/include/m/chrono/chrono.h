// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <chrono>

namespace m
{
    /// <summary>
    /// Define a standard clock for M
    /// </summary>
    using clock = std::chrono::utc_clock;

    /// <summary>
    /// Define a standard time_point for M, based on the standard M clock
    /// </summary>
    using time_point = clock::time_point;

    using utc_time_point = time_point;
}
