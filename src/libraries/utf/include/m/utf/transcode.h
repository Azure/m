// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <numeric>
#include <ranges>
#include <span>
#include <stdexcept>
#include <tuple>

#include "decode.h"
#include "decode_iterator.h"
#include "encode.h"

namespace m::utf
{
    //
    // Templatized form because the UTF-8 data can come in
    // possibly 3 different "byte" sized chunks, std::byte,
    // char, and char8_t.
    //
    template <typename SourceCharT, typename DestCharT>
    constexpr void
    transcode(std::basic_string_view<SourceCharT> v, std::basic_string<DestCharT>& str)
    {
        auto it1 = decode_begin(v);
        auto it2 = decode_end(v);

        auto const char_count0 =
            std::ranges::fold_left(it1, it2, std::size_t{}, [](std::size_t total, char32_t ch) {
                return total + m::utf::compute_encoded_char_count(DestCharT{}, ch);
            });

        str.erase();

        std::size_t char_count{};

        auto       it   = v.begin();
        auto const last = v.end();

        while (it != last)
        {
            auto [newit, ch] = m::utf::decode_utf(SourceCharT{}, it, last);
            char_count       = char_count + m::utf::compute_encoded_char_count(DestCharT{}, ch);
            it               = newit;
        }

        if (char_count != char_count0)
            throw std::runtime_error("hey what the heck??");

        str.reserve(char_count);

        it = v.begin();

        auto outit = std::back_inserter(str);

        while (it != last)
        {
            auto [newit, ch] = m::utf::decode_utf(SourceCharT{}, it, last);
            outit            = m::utf::encode_char(DestCharT{}, ch, outit);
            it               = newit;
        }
    }
} // namespace m::utf
