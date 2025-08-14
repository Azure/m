// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <concepts>
#include <type_traits>

#include <m/utility/compiler.h>

namespace m
{
    template <typename T>
        requires(std::is_enum_v<T>)
    constexpr std::underlying_type_t<T>
    to_underlying(T e)
    {
        return static_cast<std::underlying_type_t<T>>(e);
    }
} // namespace m
