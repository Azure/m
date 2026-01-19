// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <chrono>

namespace m::pil
{
    using clock_type      = std::chrono::utc_clock;
    using time_point_type = clock_type::time_point;
} // namespace m::pil
