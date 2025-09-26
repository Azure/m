// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <cstdint>

#include <m/string_buffer/string_buffer_base.h>
#include <m/string_buffer/string_buffer_overflow.h>
#include <m/string_buffer/string_buffer_overflow_provider.h>
#include <m/string_buffer/string_buffer_pooled_overflow.h>

namespace m
{
    constexpr std::size_t string_buffer_default_inline_value_count = 64;

    template <typename CharT, std::size_t N = string_buffer_default_inline_value_count>
    using basic_string_buffer = basic_string_buffer_internal_overflow<CharT, N>;

    using string_buffer    = basic_string_buffer<char>;
    using wstring_buffer   = basic_string_buffer<wchar_t>;
    using u8string_buffer  = basic_string_buffer<char8_t>;
    using u16string_buffer = basic_string_buffer<char16_t>;
    using u32string_buffer = basic_string_buffer<char32_t>;

    // This is the number of `value_type`s (chars) there are in each pool item we get
    // from the hosting pool.
    constexpr std::size_t pooled_string_buffer_default_pool_item_count = 512;

    constexpr std::size_t pooled_string_buffer_default_initial_pool_size = 256;

    constexpr std::size_t pooled_string_buffer_default_expansion_limit = 64;

    constexpr std::size_t pooled_string_buffer_default_expansion_size = 512;

    template <typename CharT,
              std::size_t NInlineValueCount = 64,
              std::size_t NPoolItemCount    = pooled_string_buffer_default_pool_item_count,
              std::size_t NInitialPoolSize  = pooled_string_buffer_default_initial_pool_size,
              std::size_t NExpansionLimit   = pooled_string_buffer_default_expansion_limit,
              std::size_t NExpansionSize    = pooled_string_buffer_default_expansion_size>
    using basic_pooled_string_buffer = basic_string_buffer_pooled_overflow<CharT,
                                                                           NInlineValueCount,
                                                                           NPoolItemCount,
                                                                           NInitialPoolSize,
                                                                           NExpansionLimit,
                                                                           NExpansionSize>;

    using pooled_string_buffer    = basic_pooled_string_buffer<char>;
    using wpooled_string_buffer   = basic_pooled_string_buffer<wchar_t>;
    using u8pooled_string_buffer  = basic_pooled_string_buffer<char8_t>;
    using u16pooled_string_buffer = basic_pooled_string_buffer<char16_t>;
    using u32pooled_string_buffer = basic_pooled_string_buffer<char32_t>;

} // namespace m
