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

#include <m/sstring/sstring.h>
#include <m/utf/decode.h>
#include <m/utf/decode_iterator.h>
#include <m/utf/encode.h>
#include <m/utility/string_inserter.h>

namespace m::utf
{
    template <typename OutCharT, typename InputIt, typename SentinelT, typename OutIt>
        requires(m::character<OutCharT> && std::forward_iterator<InputIt> &&
                 std::sized_sentinel_for<SentinelT, InputIt> 
                     // &&
                 // std::output_iterator<OutCharT, OutIt>
                     )
    OutIt
    transcode(InputIt it, SentinelT end, OutIt outit)
    {
        using input_value_type = std::iterator_traits<InputIt>::value_type;

        while (it != end)
        {
            auto [newit, ch] = m::utf::decode_utf(input_value_type{}, it, end);
            outit            = m::utf::encode_char<OutCharT>(ch, outit);
            it               = newit;
        }

        return outit;
    }

    template <typename OutCharT, typename InputIt, typename SentinelT>
        requires(std::forward_iterator<InputIt> && std::sized_sentinel_for<SentinelT, InputIt> &&
                 m::character<OutCharT>)
    void
    transcode(InputIt it, SentinelT end, std::basic_string<OutCharT>& str)
    {
        //
        // Upgrade the iterator here
        //
        transcode<OutCharT>(it, end, m::string_inserter(str));
    }

    template <typename OutCharT, typename InputIt, typename SentinelT>
        requires(std::forward_iterator<InputIt> && std::sized_sentinel_for<SentinelT, InputIt> &&
                 m::character<OutCharT>)
    auto
    transcode(InputIt it, SentinelT end)
    {
        std::basic_string<OutCharT> str;
        transcode<OutCharT>(it, end, str);
        return str;
    }

    template <typename OutCharT, typename StringishT>
        requires(m::stringish<StringishT> && m::character<OutCharT>)
    auto
    transcode(StringishT&& in)
    {
        // Perhaps these should be std::begin(in)/std::end(in) or
        // std::ranges::begin()/std::ranges::end()?
        return transcode<OutCharT>(in.begin(), in.end());
    }

    template <typename OutCharT, typename StringishT>
        requires(m::stringish<StringishT> && m::character<OutCharT>)
    auto
    transcode_to_sstring(StringishT&& in)
    {
        auto str = transcode<OutCharT>(std::forward<StringishT>(in));
        m::basic_sstring<OutCharT> ret(static_cast<std::basic_string_view<OutCharT>>(str));
        return ret;
    }

    //
    // Templatized form because the UTF-8 data can come in
    // possibly 3 different "byte" sized chunks, std::byte,
    // char, and char8_t.
    //
    template <typename SourceCharT, typename DestCharT>
        requires((sizeof(SourceCharT) == 1) && m::character<DestCharT>)
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
            outit            = m::utf::encode_char(ch, outit);
            it               = newit;
        }
    }
} // namespace m::utf
