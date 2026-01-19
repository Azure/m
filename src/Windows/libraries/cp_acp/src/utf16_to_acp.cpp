// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/cast/to.h>
#include <m/multi_byte/convert.h>
#include <m/utility/make_span.h>

#include <Windows.h>

namespace
{
    template <typename InputIt, typename TCharIn>
        requires std::input_iterator<InputIt> && std::forward_iterator<InputIt> &&
                 std::contiguous_iterator<InputIt> && m::utf16_character<TCharIn>
    void
    utf16_to_acp_impl(InputIt front, InputIt end, std::string& out)
    {
        auto const view         = std::basic_string_view<TCharIn>(front, end);
        auto const chars_needed = m::utf16_to_acp_length(view);

        out.resize_and_overwrite(chars_needed,
                                 [view](auto buffer, auto buffer_size) -> std::size_t {
                                     auto span = m::make_span(buffer, buffer_size);
                                     return m::utf16_to_acp(view, span);
                                 });
    }

} // namespace

namespace m
{
    template <>
    std::size_t
    utf16_to_acp_length(std::wstring_view in)
    {
        return utf16_to_multi_byte_length(multi_byte::cp_acp, in);
    }

    template <>
    std::size_t
    utf16_to_acp_length(std::u16string_view in)
    {
        return utf16_to_multi_byte_length(multi_byte::cp_acp, in);
    }

    template <>
    std::size_t
    utf16_to_acp_length(std::wstring_view in, std::error_code& ec)
    {
        return utf16_to_multi_byte_length(multi_byte::cp_acp, in, ec);
    }

    template <>
    std::size_t
    utf16_to_acp_length(std::u16string_view in, std::error_code& ec)
    {
        return utf16_to_multi_byte_length(multi_byte::cp_acp, in, ec);
    }

    template <>
    void
    utf16_to_acp(std::wstring_view in, std::span<char>& out)
    {
        m::utf16_to_multi_byte(multi_byte::cp_acp, in, out);
    }

    template <>
    void
    utf16_to_acp(std::u16string_view in, std::span<char>& out)
    {
        m::utf16_to_multi_byte(multi_byte::cp_acp, in, out);
    }

    template <>
    void
    utf16_to_acp(std::wstring_view in, std::span<char>& out, std::error_code& ec)
    {
        m::utf16_to_multi_byte(multi_byte::cp_acp, in, out, ec);
    }

    template <>
    void
    utf16_to_acp(std::u16string_view in, std::span<char>& out, std::error_code& ec)
    {
        m::utf16_to_multi_byte(multi_byte::cp_acp, in, out, ec);
    }

    template <>
    void
    utf16_to_acp(std::wstring_view in, std::string& out)
    {
        m::utf16_to_multi_byte(multi_byte::cp_acp, in, out);
    }

    template <>
    void
    utf16_to_acp(std::u16string_view in, std::string& out)
    {
        m::utf16_to_multi_byte(multi_byte::cp_acp, in, out);
    }

    template <>
    void
    utf16_to_acp(std::wstring_view in, std::string& out, std::error_code& ec)
    {
        m::utf16_to_multi_byte(multi_byte::cp_acp, in, out, ec);
    }

    template <>
    void
    utf16_to_acp(std::u16string_view in, std::string& out, std::error_code& ec)
    {
        m::utf16_to_multi_byte(multi_byte::cp_acp, in, out, ec);
    }

} // namespace m
