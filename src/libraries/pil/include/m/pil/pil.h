// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <initializer_list>
#include <memory>
#include <string_view>
#include <utility>

#include <m/pil/platform.h>

namespace m::pil
{
    enum class platform_type
    {
        direct,
        buffered_over_direct,
        redirecting_over_direct,
    };

    platform
    make_platform(platform_type pt);

    platform
    make_platform(platform_type pt, std::initializer_list<std::pair<std::u16string_view, std::u16string_view>> redirections);
}

