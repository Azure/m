// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
#include <gsl/gsl>

namespace m
{
    namespace utf
    {
        constexpr char32_t k_invalid_character = 0xffffffff;
        constexpr char32_t k_partial_encoding = 0xfffffffe;

        struct decode_result
        {
            char32_t    m_char;
            std::size_t m_offset;
        };
    } // namespace utf
} // namespace m
