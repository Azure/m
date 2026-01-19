// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include <Windows.h>

#include <m/errors/errors.h>
#include <m/strings/convert.h>
#include <m/strings/tstring.h>
#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utility/concepts.h>
#include <m/utility/make_span.h>
#include <m/utility/type_traits.h>

#include <m/multi_byte/code_page.h>
#include <m/multi_byte/multi_byte_to_utf16.h>
#include <m/multi_byte/utf16_to_multi_byte.h>

namespace m
{
    template <typename TCharIn, typename TCharOut>
        requires(character<TCharIn> && character<TCharOut>)
    void
    view_to_span(multi_byte::code_page           cp,
                 std::basic_string_view<TCharIn> in,
                 std::span<TCharOut>&            spn);

    template <typename TCharIn, typename TCharOut>
        requires(character<TCharIn> && character<TCharOut>)
    void
    view_to_span(multi_byte::code_page           cp,
                 std::basic_string_view<TCharIn> in,
                 std::span<TCharOut>&            spn,
                 std::error_code&                ec);

    template <typename TCharIn, typename TCharOut>
        requires(character<TCharIn> && character<TCharOut>)
    void
    to_span(multi_byte::code_page cp, std::basic_string_view<TCharIn> in, std::span<TCharOut>& spn)
    {
        view_to_span(cp, in, spn);
    }

    template <typename TCharIn, typename TCharOut>
        requires(character<TCharIn> && character<TCharOut>)
    void
    to_span(multi_byte::code_page cp, basic_zstring<TCharIn const> in, std::span<TCharOut>& spn)
    {
        std::basic_string_view<TCharIn> view{};

        if (in != nullptr)
            view = std::basic_string_view<TCharIn>(in);

        to_span(cp, view, spn);
    }

    template <typename TCharIn, typename TCharOut>
        requires character<TCharIn> && character<TCharOut>
    void
    to_span(multi_byte::code_page                                 cp,
            std::optional<std::basic_string_view<TCharIn>> const& in,
            std::span<TCharOut>&                                  spn)
    {
        if (in.has_value())
        {
            to_span(cp, in.value(), spn);
            return;
        }

        spn = std::span<TCharOut>{};
    }

    template <typename TCharIn, typename TCharOut>
        requires(m::character<TCharIn> && m::character<TCharOut>)
    void
    to_span(m::multi_byte::code_page        cp,
            m::basic_zstring<TCharIn const> in,
            std::span<TCharOut>&            spn,
            std::error_code&                ec)
    {
        std::basic_string_view<TCharIn> view{};

        if (in != nullptr)
            view = std::basic_string_view<TCharIn>(in);

        to_span(cp, view, spn, ec);
    }

    template <typename TCharIn, typename TCharOut>
        requires(m::character<TCharIn> && m::character<TCharOut>)
    void
    to_span(m::multi_byte::code_page        cp,
            std::basic_string_view<TCharIn> in,
            std::span<TCharOut>&            spn,
            std::error_code&                ec);

    template <typename TCharIn, typename TCharOut>
        requires(m::character<TCharIn> && m::character<TCharOut>)
    void
    to_span(m::multi_byte::code_page                              cp,
            std::optional<std::basic_string_view<TCharIn>> const& in,
            std::span<TCharOut>&                                  spn,
            std::error_code&                                      ec)
    {
        if (in.has_value())
        {
            to_span(cp, in.value(), spn, ec);
            return;
        }

        spn = std::span<TCharOut>{};
    }

    template <typename TCharIn, typename TCharOut>
        requires(m::character<TCharIn> && m::character<TCharOut>)
    void
    to_span(m::multi_byte::code_page        cp,
            std::basic_string_view<TCharIn> in,
            std::span<TCharOut>&            spn,
            std::error_code&                ec);

} // namespace m
