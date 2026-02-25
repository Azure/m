// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <cstdint>
#include <limits>
#include <type_traits>

#include <m/exception/exception.h>

namespace m
{
    // Smallest unsigned integral type that can represent values in [0, N].
    template <std::uintmax_t N>
    using smallest_size_t = std::conditional_t<
        (N <= (std::numeric_limits<uint8_t>::max)()),
        uint8_t,
        std::conditional_t<
            (N <= (std::numeric_limits<uint16_t>::max)()),
            uint16_t,
            std::conditional_t<(N <= (std::numeric_limits<uint32_t>::max)()),
                               uint32_t,
                               std::conditional_t<(N <= (std::numeric_limits<uint64_t>::max)()),
                                                  uint64_t,
                                                  std::uintmax_t>>>>;
} // namespace m