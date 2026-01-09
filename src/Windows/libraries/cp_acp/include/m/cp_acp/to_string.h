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
#include <m/multi_byte/code_page.h>
#include <m/strings/convert.h>
#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utility/concepts.h>
#include <m/utility/make_span.h>

#include <m/cp_acp/convert_core.h>
#include <m/cp_acp/cp_acp.h>

namespace m
{
    template <typename TStringish>
        requires any_stringish<TStringish>
    void
    acp_to_string(TStringish&& in, std::string& out)
    {
        auto const view =
            to_basic_string_view_t<stringish_char_type_t<TStringish>>(std::forward<TStringish>(in));
        acp_to_tstring(view, out);
    }

    template <typename TStringish>
        requires any_stringish<TStringish>
    void
    acp_to_string(TStringish&& in, std::string& out, std::error_code& ec)
    {
        auto const view =
            to_basic_string_view_t<stringish_char_type_t<TStringish>>(std::forward<TStringish>(in));
        acp_to_tstring(view, out, ec);
    }

    template <typename TStringish>
        requires any_stringish<TStringish>
    std::string
    acp_to_string(TStringish&& in)
    {
        std::string out;
        auto const  view =
            to_basic_string_view_t<stringish_char_type_t<TStringish>>(std::forward<TStringish>(in));
        acp_to_tstring(view, out);
        return out;
    }

    template <typename TStringish>
        requires any_stringish<TStringish>
    std::string
    acp_to_string(TStringish&& in, std::error_code& ec)
    {
        std::string out;
        auto const  view =
            to_basic_string_view_t<stringish_char_type_t<TStringish>>(std::forward<TStringish>(in));
        acp_to_tstring(view, out.value(), ec);
        return out;
    }

    template <typename TStringish>
        requires any_stringish<TStringish>
    void
    acp_to_string(std::optional<TStringish> const& in, std::optional<std::string>& out)
    {
        // Propagate nullopt
        if (!in.has_value())
        {
            out = std::nullopt;
            return;
        }

        auto const view = to_basic_string_view_t<stringish_char_type_t<TStringish>>(in.value());
        acp_to_tstring(view, out);
    }

    template <typename TStringish>
        requires any_stringish<TStringish>
    void
    acp_to_string(std::optional<TStringish> const& in,
                  std::optional<std::string>&      out,
                  std::error_code&                 ec)
    {
        // Propagate nullopt
        if (!in.has_value())
        {
            out = std::nullopt;
            return;
        }

        auto const view = to_basic_string_view_t<stringish_char_type_t<TStringish>>(in.value());
        acp_to_tstring(view, out.value(), ec);
    }

    template <typename TStringish>
        requires any_stringish<TStringish>
    std::optional<std::string>
    acp_to_string(std::optional<TStringish> const& in)
    {
        if (!in.has_value())
            return std::nullopt;

        std::string out;
        auto const  view = to_basic_string_view_t<stringish_char_type_t<TStringish>>(in.value());
        acp_to_tstring(view, out);
        return out;
    }

    template <typename TStringish>
        requires any_stringish<TStringish>
    std::optional<std::string>
    acp_to_string(std::optional<TStringish> const& in, std::error_code& ec)
    {
        if (!in.has_value())
            return std::nullopt;

        std::string out;
        auto const  view = to_basic_string_view_t<stringish_char_type_t<TStringish>>(in.value());
        acp_to_tstring(view, out, ec);
        return out;
    }

} // namespace m
