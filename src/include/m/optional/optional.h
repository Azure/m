// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <functional>
#include <optional>

namespace m
{
    template <typename T>
    class optional : public std::optional<T>
    {
        using base_class = std::optional<T>;

    public:
        // Inherit all constructors, we do not intend to define anything new
        using base_class::base_class;

        template <typename OnHasValueT, typename OnNotHasValueT>
            requires(std::invocable<OnHasValueT, T const&> && std::invocable<OnNotHasValueT>)
        void
        match(OnHasValueT&& on_has_value, OnNotHasValueT&& on_not_has_value) const
        {
            if (base_class::has_value())
                std::invoke(std::forward<OnHasValueT>(on_has_value), base_class::value());
            else
                std::invoke(std::forward<OnNotHasValueT>(on_not_has_value));
        }
    };

    template <typename ReturnT, typename T, typename OnHasValueT, typename OnNotHasValueT>
        requires(std::invocable<OnHasValueT, T const&> &&
                 std::is_invocable_r_v<ReturnT, OnHasValueT, T const&> &&
                 std::invocable<OnNotHasValueT> && std::is_invocable_r_v<ReturnT, OnNotHasValueT>)
    ReturnT
    match(std::optional<T> const& o, OnHasValueT&& on_has_value, OnNotHasValueT&& on_not_has_value)
    {
        if (o.has_value())
            return std::invoke(std::forward<OnHasValueT>(on_has_value), o.value());
        else
            return std::invoke(std::forward<OnNotHasValueT>(on_not_has_value));
    }

} // namespace m