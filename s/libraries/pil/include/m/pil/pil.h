// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <memory>

#include <m/pil/platform.h>

namespace m::pil
{
    enum class platform_type
    {
        direct,
        buffered_over_direct,
    };

    platform
    make_platform(platform_type pt);
}

