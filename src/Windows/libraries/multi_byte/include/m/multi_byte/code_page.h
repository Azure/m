// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/errors/errors.h>

#include <gsl/gsl>

#include <Windows.h>

namespace m
{
    namespace multi_byte
    {
        enum class code_page : uint16_t;

        // m::multi_byte::cp_acp is so much cooler than CP_ACP, right?
        constexpr inline code_page cp_acp = code_page{CP_ACP};
    }
} // namespace m

