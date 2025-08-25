// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <concepts>
#include <type_traits>

namespace m
{
    // Define a concept for character types
    template <typename T>
    concept character =
        std::same_as<T, char> || std::same_as<T, signed char> || std::same_as<T, unsigned char> ||
        std::same_as<T, wchar_t> || std::same_as<T, char8_t> || std::same_as<T, char16_t> ||
        std::same_as<T, char32_t>;
} // namespace m
