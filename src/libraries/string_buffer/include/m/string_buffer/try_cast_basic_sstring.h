// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <m/cast/try_cast.h>
#include <m/sstring/sstring.h>
#include <m/string_buffer/string_buffer.h>

namespace m
{
    template <typename CharT, std::size_t NInlineValueCount, typename DerivedMostStringBufferT>
    struct try_cast_helper<
        basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>,
        basic_sstring<CharT>,
        void>
    {
        static basic_sstring<CharT>
        do_cast(
            basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT> const& sb)
        {
            using string_view_t = std::basic_string_view<CharT>;

            auto const spancount =
                sb.accumulate_for_each_span(size_t{}, [](auto i, auto) { return i + 1; });

            //
            // if it's a modest number of spans, use a stack based set of arrays
            // for it, otherwise we will resort to a std::vector of spans. Hmmm
            // seems like a familiar pattern (e.g. this is what string_buffer does
            // in the first place).
            //

            std::array<string_view_t, 4> inline_string_views;

            if (spancount > inline_string_views.size())
                return out_of_line_try_cast_helper(spancount, sb);

            // We will use accumulate_for_each_span again just so we have an index
            auto const secondcount = sb.accumulate_for_each_span(
                std::size_t{}, [&inline_string_views](auto i, auto spn) {
                    inline_string_views[i] = string_view_t(spn.data(), spn.size());
                    return i + 1;
                });

            M_INTERNAL_ERROR_CHECK(secondcount == spancount);

            return basic_sstring<CharT>(
                std::span<string_view_t>(inline_string_views.data(), spancount));
        }

        M_NOINLINE static basic_sstring<CharT>
        out_of_line_try_cast_helper(
            std::size_t spancount,
            basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT> const& sb)
        {
            using string_view_t = std::basic_string_view<CharT>;
            std::vector<string_view_t> string_views(spancount);

            string_views.resize(spancount);

            // We will use accumulate_for_each_span again just so we have an index
            auto const secondcount =
                sb.accumulate_for_each_span(std::size_t{}, [&string_views](auto i, auto spn) {
                    string_views[i] = string_view_t(spn.data(), spn.size());
                    return i + 1;
                });

            M_INTERNAL_ERROR_CHECK(secondcount == spancount);

            return basic_sstring<CharT>(std::span(string_views.data(), spancount));
        }
    };
} // namespace m
