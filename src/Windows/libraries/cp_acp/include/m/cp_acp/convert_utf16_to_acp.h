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

#include <m/multi_byte/code_page.h>
#include <m/errors/errors.h>
#include <m/strings/convert.h>
#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utility/concepts.h>
#include <m/utility/make_span.h>

#include <m/cp_acp/cp_acp.h>

namespace m
{
    template <typename TCharIn>
        requires utf16_character<TCharIn>
    std::size_t
    utf16_to_acp_length(std::basic_string_view<TCharIn> in);

    template <typename TCharIn>
        requires utf16_character<TCharIn>
    std::size_t
    utf16_to_acp_length(std::basic_string_view<TCharIn> in, std::error_code& ec);

    template <typename TCharIn>
        requires utf16_character<TCharIn>
    void
    utf16_to_acp(std::basic_string_view<TCharIn> in, std::span<char>& out);

    template <typename TCharIn>
        requires utf16_character<TCharIn>
    void
    utf16_to_acp(std::basic_string_view<TCharIn> in, std::span<char>& out, std::error_code& ec);

    template <typename TCharIn>
        requires utf16_character<TCharIn>
    void
    utf16_to_acp(std::basic_string_view<TCharIn> in, std::string& string);

    template <typename TCharIn>
        requires utf16_character<TCharIn>
    void
    utf16_to_acp(std::basic_string_view<TCharIn> in, std::string& string, std::error_code& ec);
} // namespace m
