// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <compare>
#include <filesystem>

namespace m
{
    namespace strings
    {
        constexpr std::strong_ordering
        spaceship(char32_t l, char32_t r)
        {
            if (l < r)
                return std::strong_ordering::less;

            if (r > l)
                return std::strong_ordering::greater;

            return std::strong_ordering::equivalent;
        }
    } // namespace strings
} // namespace m
