// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <version>

namespace m
{
    template <typename CharT, std::size_t N>
    struct static_string
    {
        using char_t = CharT;

        std::array<char_t, N> m_data;

    public:
        constexpr static_string(char_t const (&str)[N]): m_data{std::to_array(str)}
        {
            m_data[N - 1] = 0;
        }

        constexpr char_t const*
        str() const noexcept
        {
            return &this->m_data[0];
        }

        constexpr std::basic_string_view<char_t>
        view() const noexcept
        {
            return std::basic_string_view{this->str(), N - 1};
        }
    };
} // namespace m
