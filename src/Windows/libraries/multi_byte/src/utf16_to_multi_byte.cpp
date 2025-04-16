// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/cast/to.h>
#include <m/multi_byte/multi_byte.h>

#include <Windows.h>

namespace impl
{
    template <typename Utf16CharT, typename CharTraitsT = std::char_traits<Utf16CharT>>
    std::size_t
    utf16_to_multi_byte_length_fn(m::multi_byte::code_page                        cp,
                                  std::basic_string_view<Utf16CharT, CharTraitsT> view)
    {
        auto i = ::WideCharToMultiByte(std::to_underlying(cp),
                                       WC_NO_BEST_FIT_CHARS,
                                       reinterpret_cast<wchar_t const*>(view.data()),
                                       view.size(),
                                       nullptr,
                                       0,
                                       nullptr,  // lpDefaultChar
                                       nullptr); // lpUsedDefaultChar
        if (i < 1)
            m::throw_last_win32_error();

        return m::to<std::size_t>(i);
    }

    template <typename Utf16CharT, typename CharTraitsT = std::char_traits<Utf16CharT>>
    m::windows::win32_error_code
    try_utf16_to_multi_byte_fn(m::multi_byte::code_page                        cp,
                               std::basic_string_view<Utf16CharT, CharTraitsT> view,
                               gsl::span<char>&                                buffer)
    {
        auto i = ::WideCharToMultiByte(std::to_underlying(cp),
                                       WC_NO_BEST_FIT_CHARS,
                                       reinterpret_cast<wchar_t const*>(view.data()),
                                       view.size(),
                                       buffer.data(),
                                       buffer.size(),
                                       nullptr,  // lpDefaultChar
                                       nullptr); // lpUsedDefaultChar
        if (i < 1)
            return m::windows::get_last_error();

        buffer = buffer.subspan(0, i);
        return m::windows::win32_error_code::success;
    }

    //
    template <typename Utf16CharT, typename CharTraitsT = std::char_traits<Utf16CharT>>
    std::size_t
    utf16_to_multi_byte_fn(m::multi_byte::code_page                        cp,
                           std::basic_string_view<Utf16CharT, CharTraitsT> view,
                           gsl::span<char>&                                buffer)
    {
        m::throw_if_failed(impl::try_utf16_to_multi_byte_fn(cp, view, buffer));
        return buffer.size();
    }

    template <typename Utf16CharT, typename CharTraitsT = std::char_traits<Utf16CharT>>
    std::string
    utf16_view_to_multi_byte_fn(m::multi_byte::code_page                        cp,
                                std::basic_string_view<Utf16CharT, CharTraitsT> view)
    {
        std::string result;

        auto length = utf16_to_multi_byte_length(cp, view);
        result.resize_and_overwrite(length, [&](auto buffer, auto size) -> auto {
            auto span       = gsl::make_span(buffer, size);
            auto error_code = try_utf16_to_multi_byte(cp, view, span);
            m::throw_if_failed(error_code);
            return span.size();
        });
        return result;
    }

} // namespace impl

namespace m::multi_byte
{
    namespace details
    {
        std::size_t
        utf16_to_multi_byte(code_page cp, std::wstring_view view, gsl::span<char>& buffer)
        {
            return impl::utf16_to_multi_byte_fn(cp, view, buffer);
        }

        std::size_t
        utf16_to_multi_byte(code_page cp, std::u16string_view view, gsl::span<char>& buffer)
        {
            return impl::utf16_to_multi_byte_fn(cp, view, buffer);
        }

        windows::win32_error_code
        try_utf16_to_multi_byte(code_page cp, std::wstring_view view, gsl::span<char>& buffer)
        {
            return impl::try_utf16_to_multi_byte_fn(cp, view, buffer);
        }

        windows::win32_error_code
        try_utf16_to_multi_byte(code_page cp, std::u16string_view view, gsl::span<char>& buffer)
        {
            return impl::try_utf16_to_multi_byte_fn(cp, view, buffer);
        }

        std::string
        utf16_to_multi_byte_fn(code_page cp, std::wstring_view view)
        {
            return impl::utf16_view_to_multi_byte_fn(cp, view);
        }

        std::string
        utf16_to_multi_byte_fn(code_page cp, std::u16string_view view)
        {
            return impl::utf16_view_to_multi_byte_fn(cp, view);
        }

    } // namespace details

    std::size_t
    utf16_to_multi_byte_length(code_page cp, std::wstring_view view)
    {
        return impl::utf16_to_multi_byte_length_fn(cp, view);
    }

    std::size_t
    utf16_to_multi_byte_length(code_page cp, std::u16string_view view)
    {
        return impl::utf16_to_multi_byte_length_fn(cp, view);
    }

    void
    utf16_to_multi_byte(code_page cp, std::wstring_view view, std::string& string)
    {
        string.clear();
        auto length = utf16_to_multi_byte_length(cp, view);
        string.resize_and_overwrite(length, [&](auto buffer, auto buffer_size) -> auto {
            auto span = gsl::make_span(buffer, buffer_size);
            utf16_to_multi_byte(cp, view, span);
            return span.size();
        });
    }

    void
    utf16_to_multi_byte(code_page cp, std::u16string_view view, std::string& string)
    {
        string.clear();
        auto length = utf16_to_multi_byte_length(cp, view);
        string.resize_and_overwrite(length, [&](auto buffer, auto buffer_size) -> auto {
            auto span = gsl::make_span(buffer, buffer_size);
            utf16_to_multi_byte(cp, view, span);
            return span.size();
        });
    }

    void
    utf16_to_acp(std::wstring_view const& view, std::string& string)
    {
        utf16_to_multi_byte(cp_acp, view, string);
    }

    void
    utf16_to_acp(std::u16string_view const& view, std::string& string)
    {
        utf16_to_multi_byte(cp_acp, view, string);
    }

} // namespace m::multi_byte

std::string
m::to_string(std::wstring_view const& view)
{
    std::string string;
    m::multi_byte::utf16_to_acp(view, string);
    return string;
}

std::string
m::to_string(std::u16string_view const& view)
{
    std::string string;
    m::multi_byte::utf16_to_acp(view, string);
    return string;
}

std::string
m::to_string(m::multi_byte::code_page cp, std::wstring_view const& view)
{
    std::string string;
    m::multi_byte::utf16_to_multi_byte(cp, view, string);
    return string;
}

std::string
m::to_string(m::multi_byte::code_page cp, std::u16string_view const& view)
{
    std::string string;
    m::multi_byte::utf16_to_multi_byte(cp, view, string);
    return string;
}
