// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstddef>
#include <string_view>

//
// These are relatively type-unsafe coercions which assume that
// two types are the same size and alignment and therefore that
// views of one sort can be used equally as views of another
// sort.
// 
// Primarily this is to enable playing fast and loose between
// the type-safe char8_t which is well-defined as indicating that
// basic_string<char8_t> (a/k/a u8string) is encoded using UTF-8
// and basic_string<char> (a/k/a std::string) which on OSS from
// the Unix/Linux world commonly assumes that std::string is
// UTF-8 encoded.
// 
// The idempotent functions are provided so that code can be written
// to not care which platform is being targeted in case there is some
// variance.
//

namespace m
{
        //
        // Should these really take <foo>_view&& ?
        // 
        // The lifetime management issues still confound me.
        //

        std::u8string_view
        as_u8string_view(std::string_view const& view);

        std::u8string_view
        as_u8string_view(std::u8string_view const& view);

        std::string_view
        as_string_view(std::u8string_view const& view);

        std::string_view
        as_string_view(std::string_view const& view);
} // namespace m
