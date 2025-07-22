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

namespace m
{
    template <typename Fn, typename... Args>
    void
    n_times(std::size_t n, Fn&& fn, Args&&... args)
    {
        auto const captured_args = std::forward_as_tuple(std::forward<Args>(args)...);

        for (std::size_t i = 0; i < n; i++)
            std::apply(fn, captured_args);
    }
} // namespace m