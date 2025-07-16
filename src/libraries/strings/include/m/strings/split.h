// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace m
{
    template <typename CharT>
    struct split_basic_string_result
    {
        std::basic_string_view<CharT>                item;
        std::optional<std::basic_string_view<CharT>> remainder;
    };

    template <typename CharT>
    split_basic_string_result<CharT>
    split_basic_string(std::basic_string_view<CharT> str, CharT ch)
    {
        auto const pos = str.find(ch);
        if (pos == std::basic_string_view<CharT>::npos)
        {
            // No delimiter found so return the whole thing in item and
            // nullopt in remainder.
            return split_basic_string_result<CharT>{.item = str, .remainder = std::nullopt};
        }

        // The returned item starts at offset 0 and goes for 'pos' chars
        //
        // The way to reason about this is that if the delimiter were in
        // the first characer position, 'pos' would be zero, so we would
        // want to return a zero length item.
        //
        auto const item = std::basic_string_view<CharT>(str.data(), pos);

        // The next item in the sequence starts at offset pos+1 and
        // has length size() - (pos+1).
        auto const offset = (pos + 1);
        auto const remainder =
            std::basic_string_view<CharT>(str.data() + offset, str.size() - offset);
        return split_basic_string_result<CharT>{.item = item, .remainder = remainder};
    }

} // namespace m
