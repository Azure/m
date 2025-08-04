// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
#include <type_traits>

namespace m
{
    namespace tracing
    {
        enum class topology_version : uint64_t;

        template <typename IntegralT>
            requires(std::is_integral_v<IntegralT>)
        constexpr inline topology_version&
        operator+=(topology_version& l, IntegralT r)
        {
            l = topology_version{std::to_underlying(l) + r};
            return l;
        }

        template <typename IntegralT>
            requires(std::is_integral_v<IntegralT>)
        constexpr inline topology_version
        operator+(topology_version l, IntegralT r)
        {
            return topology_version{std::to_underlying(l) + r};
        }

        template <typename IntegralT>
            requires(std::is_integral_v<IntegralT>)
        constexpr inline topology_version
        operator-(topology_version l, IntegralT r)
        {
            return topology_version{std::to_underlying(l) - r};
        }
    } // namespace tracing
} // namespace m
