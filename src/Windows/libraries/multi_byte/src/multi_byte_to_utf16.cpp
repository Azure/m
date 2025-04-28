// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/cast/to.h>
#include <m/multi_byte/multi_byte.h>

#include <Windows.h>

std::size_t
m::multi_byte::multi_byte_to_utf16_length(code_page cp, std::string_view view)
{
    if (view.size() == 0)
        return 0;

    int wchars_needed = ::MultiByteToWideChar(
        std::to_underlying(cp), MB_ERR_INVALID_CHARS, view.data(), view.size(), nullptr, 0);
    if (wchars_needed < 1)
        throw_last_win32_error();

    return m::to<std::size_t>(wchars_needed);
}

namespace
{
    template <typename TUtf16Char>
    m::windows::win32_error_code
    multi_byte_to_utf16_fn(m::multi_byte::code_page cp,
                           std::string_view         view,
                           std::span<TUtf16Char>&   buffer)
        requires std::is_same_v<TUtf16Char, wchar_t> || std::is_same_v<TUtf16Char, char16_t>
    {
        if (view.size() == 0)
        {
            buffer = buffer.subspan(0, 0);
            return m::windows::win32_error_code::success;
        }

        auto const i = ::MultiByteToWideChar(std::to_underlying(cp),
                                             MB_ERR_INVALID_CHARS,
                                             view.data(),
                                             view.size(),
                                             reinterpret_cast<LPWSTR>(buffer.data()),
                                             buffer.size());
        if (i < 1)
            return m::windows::get_last_error();

        auto const i_as_sizet = m::to<std::size_t>(i);

        buffer = buffer.subspan(0, i_as_sizet);
        return m::windows::win32_error_code::success;
    }
} // namespace

std::size_t
m::multi_byte::multi_byte_to_utf16(m::multi_byte::code_page cp,
                                   std::string_view         view,
                                   std::span<wchar_t>&      span)
{
    throw_if_failed(multi_byte_to_utf16_fn(cp, view, span));
    return span.size();
}

std::size_t
m::multi_byte::multi_byte_to_utf16(m::multi_byte::code_page cp,
                                   std::string_view         view,
                                   std::span<char16_t>&     span)
{
    throw_if_failed(multi_byte_to_utf16_fn(cp, view, span));
    return span.size();
}

m::windows::win32_error_code
m::multi_byte::try_multi_byte_to_utf16(m::multi_byte::code_page cp,
                                       std::string_view         view,
                                       std::span<wchar_t>&      span)
{
    return multi_byte_to_utf16_fn(cp, view, span);
}

m::windows::win32_error_code
m::multi_byte::try_multi_byte_to_utf16(m::multi_byte::code_page cp,
                                       std::string_view         view,
                                       std::span<char16_t>&     span)
{
    return multi_byte_to_utf16_fn(cp, view, span);
}

void
m::multi_byte::multi_byte_to_utf16(m::multi_byte::code_page cp,
                                   std::string_view         view,
                                   std::wstring&            string)
{
    string.clear();
    auto it = std::back_inserter(string);
    multi_byte_to_utf16(cp, view, it);
}

void
m::multi_byte::multi_byte_to_utf16(m::multi_byte::code_page cp,
                                   std::string_view         view,
                                   std::u16string&          string)
{
    string.clear();
    auto it = std::back_inserter(string);
    multi_byte_to_utf16(cp, view, it);
}

std::size_t
m::multi_byte::acp_to_utf16_length(std::string_view view)
{
    return multi_byte_to_utf16_length(cp_acp, view);
}

std::size_t
m::multi_byte::acp_to_utf16(std::string_view view, std::span<wchar_t>& buffer)
{
    return multi_byte_to_utf16(cp_acp, view, buffer);
}

std::size_t
m::multi_byte::acp_to_utf16(std::string_view view, std::span<char16_t>& buffer)
{
    return multi_byte_to_utf16(cp_acp, view, buffer);
}

std::wstring
m::to_wstring_acp(std::string_view view)
{
    std::wstring string;
    //    auto         outit = std::back_inserter(string);
    m::multi_byte::acp_to_utf16(view, string);
    return string;
}

std::u16string
m::to_u16string_acp(std::string_view view)
{
    std::u16string string;
    //    auto         outit = std::back_inserter(string);
    m::multi_byte::acp_to_utf16(view, string);
    return string;
}

std::wstring
m::to_wstring(m::multi_byte::code_page cp, std::string_view view)
{
    std::wstring string;
    m::multi_byte::multi_byte_to_utf16(cp, view, string);
    return string;
}

std::u16string
m::to_u16string(m::multi_byte::code_page cp, std::string_view view)
{
    std::u16string string;
    m::multi_byte::multi_byte_to_utf16(cp, view, string);
    return string;
}
