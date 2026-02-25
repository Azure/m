// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/utility/concepts.h>
#include <m/utility/pointers.h>
#include <m/utility/stringish.h>
#include <m/utility/type_traits.h>

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

    template <typename T>
    struct view_of_helper;

    template <typename CharT>
        requires m::character<CharT>
    struct view_of_helper<basic_sstring<CharT>>
    {
        static auto
        get_view_of(basic_sstring<CharT> const& str)
        {
            return str.view();
        }
    };

    template <typename CharT>
        requires m::character<CharT>
    struct view_of_helper<std::optional<basic_sstring<CharT>>>
    {
        static std::optional<std::basic_string_view<CharT>>
        get_view_of(std::optional<basic_sstring<CharT>> const& str)
        {
            if (!str.has_value())
                return std::nullopt;

            return str.value().view();
        }
    };

    template <typename CharT>
        requires m::character<CharT>
    struct view_of_helper<CharT const*>
    {
        static auto
        get_view_of(CharT const* str)
        {
            if (str == nullptr)
                return std::basic_string_view<CharT>();

            return std::basic_string_view<CharT>(str);
        }
    };

    template <typename CharT>
        requires m::character<CharT>
    struct view_of_helper<not_null<CharT const*>>
    {
        static auto
        get_view_of(not_null<CharT const*> str)
        {
            return std::basic_string_view<CharT>(str);
        }
    };

    template <typename CharT>
        requires m::character<CharT>
    struct view_of_helper<std::basic_string_view<CharT>>
    {
        static auto
        get_view_of(std::basic_string_view<CharT> const& view)
        {
            return view;
        }
    };

    template <typename CharT>
        requires m::character<CharT>
    struct view_of_helper<std::optional<std::basic_string_view<CharT>>>
    {
        static auto
        get_view_of(std::optional<std::basic_string_view<CharT>> const& view)
        {
            return view;
        }
    };

    template <typename CharT>
        requires m::character<CharT>
    struct view_of_helper<std::basic_string<CharT>>
    {
        static auto
        get_view_of(std::basic_string<CharT> const& str)
        {
            return static_cast<std::basic_string_view<CharT>>(str);
        }
    };

    template <typename CharT>
        requires m::character<CharT>
    struct view_of_helper<std::optional<std::basic_string<CharT>>>
    {
        static std::optional<std::basic_string_view<CharT>>
        get_view_of(std::optional<std::basic_string<CharT>> const& str)
        {
            if (!str.has_value())
                return std::nullopt;

            return static_cast<std::basic_string_view<CharT>>(str.value());
        }
    };

    template <typename CharT, std::size_t N>
        requires m::character<CharT>
    struct view_of_helper<CharT[N]>
    {
        static std::basic_string_view<CharT>
        get_view_of(CharT const* ptr)
        {
            return std::basic_string_view<CharT>(ptr, N);
        }
    };

    template <typename T>
    auto
    view_of(T&& v)
    {
        return view_of_helper<remove_cvref_t<T>>::get_view_of(std::forward<T>(v));
    }
} // namespace m