// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstddef>
#include <span>

namespace m
{
    // More-or-less copy of gsl::basic_zstring<>

    template <typename CharT, std::size_t Extent = std::dynamic_extent>
    using basic_zstring = CharT*;

    using czstring = basic_zstring<char const , std::dynamic_extent>;

    using cwzstring = basic_zstring<wchar_t const, std::dynamic_extent>;

    using cu16zstring = basic_zstring<char16_t const, std::dynamic_extent>;

    using cu32zstring = basic_zstring<char32_t const, std::dynamic_extent>;

    using zstring = basic_zstring<char, std::dynamic_extent>;

    using wzstring = basic_zstring<wchar_t, std::dynamic_extent>;

    using u16zstring = basic_zstring<char16_t, std::dynamic_extent>;

    using u32zstring = basic_zstring<char32_t, std::dynamic_extent>;

} // namespace m
