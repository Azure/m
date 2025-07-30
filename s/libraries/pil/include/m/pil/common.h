// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <chrono>

namespace m::pil
{
    using clock = std::chrono::utc_clock;
    using time_point = clock::time_point;
} // namespace m::pil
