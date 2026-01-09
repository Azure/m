// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/cast/to.h>
#include <m/multi_byte/convert.h>
#include <m/utility/make_span.h>

#include <m/cp_acp/convert.h>

#include <Windows.h>

namespace m
{
    std::size_t
    acp_to_char32_length(std::string_view in)
    {
        M_NOT_IMPLEMENTED("Not yet implemented");
    }

    std::size_t
    acp_to_char32_length(std::string_view in, std::error_code& ec)
    {
        M_NOT_IMPLEMENTED("Not yet implemented");
    }

    template <>
    void
    acp_to_span(std::string_view in, std::span<char32_t>& out)
    {
        M_NOT_IMPLEMENTED("Not yet implemented");
    }

    template <>
    void
    acp_to_span(std::string_view in, std::span<char32_t>& out, std::error_code& ec)
    {
        M_NOT_IMPLEMENTED("Not yet implemented");
    }

    void
    acp_to_u32string(std::string_view in, std::u32string& out)
    {
        acp_to_basic_string(in, out);
    }

    void
    acp_to_u32string(std::string_view in, std::u32string& out, std::error_code& ec)
    {
        acp_to_basic_string(in, out, ec);
    }

    std::u32string
    acp_to_u32string(std::string_view in)
    {
        std::u32string out;
        acp_to_u32string(in, out);
        return out;
    }

    std::u32string
    acp_to_u32string(std::string_view in, std::error_code& ec)
    {
        std::u32string out;
        acp_to_u32string(in, out, ec);
        return out;
    }

} // namespace m