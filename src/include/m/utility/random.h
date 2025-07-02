// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <random>
#include <span>
#include <type_traits>

#include <m/utility/unique_span.h>

namespace m::random
{
    template <typename Generator>
    m::unique_span<std::byte>
    make_unique_byte_span(std::size_t size, Generator& g)
    {
        using byte_t = std::underlying_type_t<std::byte>;

        std::uniform_int_distribution<> distribution((std::numeric_limits<byte_t>::min)(),
                                                     (std::numeric_limits<byte_t>::max)());

        return m::unique_span<std::byte>(size, [&](std::size_t, std::byte& b) { b = std::byte{static_cast<byte_t>(distribution(g))}; });
    }

} // namespace m::random