// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include <m/cp_acp/convert_acp_to.h>
#include <m/strings/convert.h>
#include <m/utility/concepts.h>

namespace m
{
    //
    // Optional overloads for acp_to_string.  These supplement the non-optional overloads
    // provided in convert_acp_to.h and propagate std::nullopt when the input is absent.
    //

    template <typename TStringish>
        requires stringish<TStringish, char>
    void
    acp_to_string(std::optional<TStringish> const& in, std::optional<std::string>& out)
    {
        if (!in.has_value())
        {
            out = std::nullopt;
            return;
        }

        auto const view = to_basic_string_view_t<char>(in.value());
        out.emplace();
        acp_to_basic_string(view, *out);
    }

    template <typename TStringish>
        requires stringish<TStringish, char>
    void
    acp_to_string(std::optional<TStringish> const& in,
                  std::optional<std::string>&       out,
                  std::error_code&                  ec)
    {
        if (!in.has_value())
        {
            out = std::nullopt;
            return;
        }

        auto const view = to_basic_string_view_t<char>(in.value());
        out.emplace();
        acp_to_basic_string(view, *out, ec);
    }

    template <typename TStringish>
        requires stringish<TStringish, char>
    std::optional<std::string>
    acp_to_string(std::optional<TStringish> const& in)
    {
        if (!in.has_value())
            return std::nullopt;

        std::string out;
        auto const  view = to_basic_string_view_t<char>(in.value());
        acp_to_basic_string(view, out);
        return out;
    }

    template <typename TStringish>
        requires stringish<TStringish, char>
    std::optional<std::string>
    acp_to_string(std::optional<TStringish> const& in, std::error_code& ec)
    {
        if (!in.has_value())
            return std::nullopt;

        std::string out;
        auto const  view = to_basic_string_view_t<char>(in.value());
        acp_to_basic_string(view, out, ec);
        return out;
    }

} // namespace m
