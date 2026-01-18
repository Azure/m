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

namespace
{
    template <typename TCharOut>
        requires m::utf16_character<TCharOut>
    void
    acp_to_utf16_impl(std::string_view in, std::basic_string<TCharOut>& out, std::error_code& ec)
    {
        auto const wchars_needed = m::acp_to_utf16_length(in, ec);

        if (m::failed(ec))
            return;

        out.resize_and_overwrite(wchars_needed,
                                 [in, &ec](auto buffer, auto buffer_size) -> std::size_t {
                                     auto span = m::make_span(buffer, buffer_size);
                                     m::acp_to_utf16(in, span, ec);
                                     return span.size();
                                 });
    }

    template <typename TCharOut>
        requires m::utf16_character<TCharOut>
    void
    acp_to_utf16_impl(std::string_view in, std::basic_string<TCharOut>& out)
    {
        auto const wchars_needed = m::acp_to_utf16_length(in);

        out.resize_and_overwrite(wchars_needed, [in](auto buffer, auto buffer_size) -> std::size_t {
            auto span = m::make_span(buffer, buffer_size);
            m::acp_to_utf16(in, span);
            return span.size();
        });
    }

    template <typename TInputIterator, typename TSentinel, typename TCharOut>
        requires std::input_iterator<TInputIterator> && std::forward_iterator<TInputIterator> &&
                 std::contiguous_iterator<TInputIterator> &&
                 std::sized_sentinel_for<TSentinel, TInputIterator> &&
                 std::is_same_v<std::iter_value_t<TInputIterator>, char> &&
                 m::utf16_character<TCharOut>
    void
    acp_to_utf16_impl(TInputIterator front, TSentinel end, std::basic_string<TCharOut>& out)
    {
        auto const view = std::string_view(front, end);
        acp_to_utf16_impl(view, out);
    }
} // namespace

namespace m
{
    std::size_t
    acp_to_utf16_length(std::string_view in)
    {
        return m::multi_byte_to_utf16_length(multi_byte::cp_acp, in);
    }

    std::size_t
    acp_to_utf16_length(std::string_view in, std::error_code& ec)
    {
        return m::multi_byte_to_utf16_length(multi_byte::cp_acp, in, ec);
    }

    template <>
    void
    acp_to_utf16(std::string_view in, std::wstring& out)
    {
        acp_to_utf16_impl(in, out);
    }

    template <>
    void
    acp_to_utf16(std::string_view in, std::u16string& out)
    {
        acp_to_utf16_impl(in, out);
    }

    template <>
    void
    acp_to_utf16(std::string_view in, std::wstring& out, std::error_code& ec)
    {
        acp_to_utf16_impl(in, out, ec);
    }

    template <>
    void
    acp_to_utf16(std::string_view in, std::u16string& out, std::error_code& ec)
    {
        acp_to_utf16_impl(in, out, ec);
    }

    template <>
    void
    acp_to_utf16(std::string_view in, std::span<wchar_t>& out)
    {
        m::multi_byte_to_utf16(m::multi_byte::cp_acp, in, out);
    }

    template <>
    void
    acp_to_utf16(std::string_view in, std::span<char16_t>& out)
    {
        m::multi_byte_to_utf16(m::multi_byte::cp_acp, in, out);
    }

    template <>
    void
    acp_to_utf16(std::string_view in, std::span<wchar_t>& out, std::error_code& ec)
    {
        m::multi_byte_to_utf16(m::multi_byte::cp_acp, in, out, ec);
    }

    template <>
    void
    acp_to_utf16(std::string_view in, std::span<char16_t>& out, std::error_code& ec)
    {
        m::multi_byte_to_utf16(m::multi_byte::cp_acp, in, out, ec);
    }

} // namespace m