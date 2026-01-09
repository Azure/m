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
    acp_to_char_length(std::string_view in)
    {
        return in.size();
    }

    std::size_t
    acp_to_char_length(std::string_view in, std::error_code&)
    {
        return in.size();
    }

    template <>
    void
    acp_to_span(std::string_view in, std::span<char>& out)
    {
        M_NOT_IMPLEMENTED("Not yet implemented");
    }

    template <>
    void
    acp_to_span(std::string_view in, std::span<char>& out, std::error_code& ec)
    {
        M_NOT_IMPLEMENTED("Not yet implemented");
    }

    void
    acp_to_string(std::string_view in, std::string& out)
    {
        acp_to_basic_string(in, out);
    }

    void
    acp_to_string(std::string_view in, std::string& out, std::error_code& ec)
    {
        acp_to_basic_string(in, out, ec);
    }

    std::string
    acp_to_string(std::string_view in)
    {
        std::string out;
        acp_to_string(in, out);
        return out;
    }

    std::string
    acp_to_string(std::string_view in, std::error_code& ec)
    {
        std::string out;
        acp_to_string(in, out, ec);
        return out;
    }

} // namespace m