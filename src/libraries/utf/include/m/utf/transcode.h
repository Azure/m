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

#include <m/math/math.h>
#include <m/sstring/sstring.h>
#include <m/utf/decode.h>
#include <m/utf/decode_iterator.h>
#include <m/utf/encode.h>
#include <m/utility/make_span.h>
#include <m/utility/string_inserter.h>

namespace m::utf
{
    template <typename TCharOut, typename InputIt, typename SentinelT, typename OutIt>
        requires(m::character<TCharOut> && std::forward_iterator<InputIt> &&
                 std::sized_sentinel_for<SentinelT, InputIt>
                 // &&
                 // std::output_iterator<TCharOut, OutIt>
                 )
    OutIt
    transcode(InputIt it, SentinelT end, OutIt outit)
    {
        using input_value_type = std::iterator_traits<InputIt>::value_type;

        while (it != end)
        {
            auto [newit, ch] = decode_utf(input_value_type{}, it, end);
            outit            = encode_char<TCharOut>(ch, outit);
            it               = newit;
        }

        return outit;
    }

    template <typename TCharOut, typename InputIt, typename SentinelT, typename OutIt>
        requires(m::character<TCharOut> && std::forward_iterator<InputIt> &&
                 std::sized_sentinel_for<SentinelT, InputIt>
                 // &&
                 // std::output_iterator<TCharOut, OutIt>
                 )
    OutIt
    transcode(InputIt it, SentinelT end, OutIt outit, std::error_code& ec)
    {
        using input_value_type = std::iterator_traits<InputIt>::value_type;

        while (it != end)
        {
            auto [newit, ch] = m::utf::decode_utf(input_value_type{}, it, end, ec);
            if (ec)
                return outit;

            outit = m::utf::encode_char<TCharOut>(ch, outit, ec);
            if (ec)
                return outit;

            it = newit;
        }

        return outit;
    }

    template <typename TCharOut, typename InputIt, typename SentinelT>
        requires(std::forward_iterator<InputIt> && std::sized_sentinel_for<SentinelT, InputIt> &&
                 m::character<TCharOut>)
    void
    transcode(InputIt it, SentinelT end, std::basic_string<TCharOut>& out)
    {
        std::basic_string<TCharOut> temp;
        //
        // Upgrade the iterator here
        //
        transcode<TCharOut>(it, end, m::string_inserter(temp));

        using std::swap;
        swap(temp, out);
    }

    template <typename TCharOut, typename InputIt, typename SentinelT>
        requires(std::forward_iterator<InputIt> && std::sized_sentinel_for<SentinelT, InputIt> &&
                 m::character<TCharOut>)
    void
    transcode(InputIt it, SentinelT end, std::basic_string<TCharOut>& out, std::error_code& ec)
    {
        std::basic_string<TCharOut> temp;
        //
        // Upgrade the iterator here
        //
        transcode<TCharOut>(it, end, m::string_inserter(temp), ec);

        if (ec)
            return;

        using std::swap;
        swap(temp, out);
    }

    template <typename TCharOut, typename InputIt, typename SentinelT>
        requires(std::forward_iterator<InputIt> && std::sized_sentinel_for<SentinelT, InputIt> &&
                 m::character<TCharOut>)
    auto
    transcode(InputIt it, SentinelT end)
    {
        std::basic_string<TCharOut> str;
        transcode<TCharOut>(it, end, str);
        return str;
    }

    template <typename TCharOut, typename TStringish>
        requires(m::any_stringish<TStringish> && m::character<TCharOut>)
    auto
    transcode(TStringish&& in)
    {
        // Perhaps these should be std::begin(in)/std::end(in) or
        // std::ranges::begin()/std::ranges::end()?
        return transcode<TCharOut>(in.begin(), in.end());
    }

    template <typename TCharOut, typename TStringish>
        requires(m::any_stringish<TStringish> && m::character<TCharOut>)
    void
    transcode(TStringish&& in, std::basic_string<TCharOut>& out)
    {
        // Perhaps these should be std::begin(in)/std::end(in) or
        // std::ranges::begin()/std::ranges::end()?
        transcode(std::ranges::begin(in), std::ranges::end(in), out);
    }

    template <typename TCharOut, typename TStringish>
        requires(m::any_stringish<TStringish> && m::character<TCharOut>)
    void
    transcode(TStringish&& in, std::basic_string<TCharOut>& out, std::error_code& ec)
    {
        // Perhaps these should be std::begin(in)/std::end(in) or
        // std::ranges::begin()/std::ranges::end()?
        transcode(std::ranges::begin(in), std::ranges::end(in), out, ec);
    }

    template <typename TCharOut, typename TStringish>
        requires(m::any_stringish<TStringish> && m::character<TCharOut>)
    auto
    transcode_to_sstring(TStringish&& in)
    {
        auto                       str = transcode<TCharOut>(std::forward<TStringish>(in));
        m::basic_sstring<TCharOut> ret(static_cast<std::basic_string_view<TCharOut>>(str));
        return ret;
    }

    //
    // Templatized form because the UTF-8 data can come in
    // possibly 3 different "byte" sized chunks, std::byte,
    // char, and char8_t.
    //
    template <typename TCharIn, typename TCharOut>
        requires((sizeof(TCharIn) == 1) && m::character<TCharOut>)
    constexpr void
    transcode(std::basic_string_view<TCharIn> in, std::basic_string<TCharOut>& out)
    {
        std::size_t char_count{};

        auto       it   = in.begin();
        auto const last = in.end();

        while (it != last)
        {
            auto [newit, ch] = decode_utf(TCharIn{}, it, last);
            char_count =
                math::add(char_count, compute_encoded_char_count(TCharOut{}, ch), std::size_t{});
            it = newit;
        }

        it = in.begin();

        std::basic_string<TCharOut> newout;
        newout.resize_and_overwrite(char_count,
                                    [&it, &last](auto buffer, auto buffer_size) -> std::size_t {
                                        auto span  = m::make_span(buffer, buffer_size);
                                        auto outit = span.begin();

                                        while (it != last)
                                        {
                                            auto [newit, ch] = decode_utf(TCharIn{}, it, last);
                                            outit            = encode_char<TCharOut>(ch, outit);
                                            it               = newit;
                                        }

                                        return static_cast<std::size_t>(outit - span.begin());
                                    });

        using std::swap;
        swap(out, newout);
    }

    template <typename TCharIn, typename TCharOut>
        requires((sizeof(TCharIn) == 1) && m::character<TCharOut>)
    constexpr void
    transcode(std::basic_string_view<TCharIn> in,
              std::basic_string<TCharOut>&    out,
              std::error_code&                ec)
    {
        std::size_t char_count{};

        auto       it   = in.begin();
        auto const last = in.end();

        while (it != last)
        {
            auto [newit, ch] = decode_utf(TCharIn{}, it, last, ec);

            if (ec)
                return;

            char_count =
                math::add(char_count, compute_encoded_char_count(TCharOut{}, ch), std::size_t{});
            it = newit;
        }

        it = in.begin();

        std::basic_string<TCharOut> newout;
        newout.resize_and_overwrite(
            char_count, [&it, &last](auto buffer, auto buffer_size) -> std::size_t {
                auto span  = m::make_span(buffer, buffer_size);
                auto outit = span.begin();

                while (it != last)
                {
                    auto [newit, ch] = m::utf::decode_utf(TCharIn{}, it, last);
                    outit            = m::utf::encode_char<TCharOut>(ch, outit);
                    it               = newit;
                }

                return static_cast<std::size_t>(outit - span.begin());
            });

        using std::swap;
        swap(out, newout);
    }

} // namespace m::utf
