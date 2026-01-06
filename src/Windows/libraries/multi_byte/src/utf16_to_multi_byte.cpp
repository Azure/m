// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/cast/to.h>
#include <m/error_handling/macros.h>
#include <m/multi_byte/convert.h>
#include <m/utility/make_span.h>

#include <Windows.h>

namespace impl
{
    template <typename Utf16CharT, typename CharTraitsT = std::char_traits<Utf16CharT>>
    std::size_t
    utf16_to_multi_byte_length_fn(m::multi_byte::code_page                        cp,
                                  std::basic_string_view<Utf16CharT, CharTraitsT> view)
    {
        auto const view_size = view.size();
        if (view_size == 0)
            return 0;

        auto i = ::WideCharToMultiByte(std::to_underlying(cp),
                                       WC_NO_BEST_FIT_CHARS,
                                       reinterpret_cast<wchar_t const*>(view.data()),
                                       m::to<int>(view_size),
                                       nullptr,
                                       0,
                                       nullptr,  // lpDefaultChar
                                       nullptr); // lpUsedDefaultChar
        if (i < 1)
            m::throw_last_win32_error();

        return m::to<std::size_t>(i);
    }

    template <typename Utf16CharT, typename CharTraitsT = std::char_traits<Utf16CharT>>
    void
    utf16_to_multi_byte_fn(m::multi_byte::code_page                        cp,
                           std::basic_string_view<Utf16CharT, CharTraitsT> view,
                           std::span<char>&                                buffer,
                           std::error_code&                                ec)
    {
        auto const view_size = view.size();
        if (view_size == 0)
        {
            //
            // The WideCharToMultiByte() API is not happy getting an
            // input of 0 length so we handle it separately.
            //
            buffer = buffer.subspan(0, 0);
            return;
        }

        auto i = ::WideCharToMultiByte(std::to_underlying(cp),
                                       WC_NO_BEST_FIT_CHARS,
                                       reinterpret_cast<wchar_t const*>(view.data()),
                                       m::to<int>(view.size()),
                                       buffer.data(),
                                       m::to<int>(buffer.size()),
                                       nullptr,  // lpDefaultChar
                                       nullptr); // lpUsedDefaultChar
        if (i < 1)
        {
            ec = m::get_last_win32_error();
            return;
        }

        buffer = buffer.subspan(0, i);
    }

    template <typename Utf16CharT>
    std::size_t
    utf16_to_multi_byte_fn(m::multi_byte::code_page           cp,
                           std::basic_string_view<Utf16CharT> view,
                           std::span<char>&                   buffer)
    {
        std::error_code ec;
        impl::utf16_to_multi_byte_fn(cp, view, buffer, ec);
        m::throw_if_failed(ec);
        return buffer.size();
    }

    template <typename Utf16CharT, typename CharTraitsT = std::char_traits<Utf16CharT>>
    std::string
    utf16_view_to_multi_byte_fn(m::multi_byte::code_page                        cp,
                                std::basic_string_view<Utf16CharT, CharTraitsT> view)
    {
        std::string result;

        auto length = utf16_to_multi_byte_length_fn(cp, view);
        result.resize_and_overwrite(length, [&](auto buffer, auto size) -> auto {
            auto            span = m::make_span(buffer, size);
            std::error_code ec;
            m::utf16_to_multi_byte(cp, view, span, ec);
            m::throw_if_failed(ec);
            return span.size();
        });
        return result;
    }

} // namespace impl

namespace m
{
    std::size_t
    utf16_to_multi_byte(multi_byte::code_page cp, std::wstring_view view, std::span<char>& buffer)
    {
        return impl::utf16_to_multi_byte_fn(cp, view, buffer);
    }

    std::size_t
    utf16_to_multi_byte(multi_byte::code_page cp, std::u16string_view view, std::span<char>& buffer)
    {
        return impl::utf16_to_multi_byte_fn(cp, view, buffer);
    }

    void
    utf16_to_multi_byte(multi_byte::code_page cp,
                        std::wstring_view     view,
                        std::span<char>&      buffer,
                        std::error_code&      ec)
    {
        return impl::utf16_to_multi_byte_fn(cp, view, buffer, ec);
    }

    void
    utf16_to_multi_byte(multi_byte::code_page cp,
                        std::u16string_view   view,
                        std::span<char>&      buffer,
                        std::error_code&      ec)
    {
        return impl::utf16_to_multi_byte_fn(cp, view, buffer, ec);
    }

    std::string
    utf16_to_multi_byte_fn(multi_byte::code_page cp, std::wstring_view view)
    {
        return impl::utf16_view_to_multi_byte_fn(cp, view);
    }

    std::string
    utf16_to_multi_byte_fn(multi_byte::code_page cp, std::u16string_view view)
    {
        return impl::utf16_view_to_multi_byte_fn(cp, view);
    }

    std::size_t
    utf16_to_multi_byte_length(multi_byte::code_page cp, std::wstring_view view)
    {
        return impl::utf16_to_multi_byte_length_fn(cp, view);
    }

    std::size_t
    utf16_to_multi_byte_length(multi_byte::code_page cp, std::u16string_view view)
    {
        return impl::utf16_to_multi_byte_length_fn(cp, view);
    }

    void
    utf16_to_multi_byte(multi_byte::code_page cp, std::wstring_view view, std::string& str)
    {
        std::string t;
        auto        length = utf16_to_multi_byte_length(cp, view);
        t.resize_and_overwrite(length, [&](auto buffer, auto buffer_size) -> auto {
            auto span = m::make_span(buffer, buffer_size);
            utf16_to_multi_byte(cp, view, span);
            return span.size();
        });
        using std::swap;
        swap(t, str);
    }

    void
    utf16_to_multi_byte(multi_byte::code_page cp, std::u16string_view view, std::string& str)
    {
        std::string t;
        auto        length = utf16_to_multi_byte_length(cp, view);
        t.resize_and_overwrite(length, [&](auto buffer, auto buffer_size) -> auto {
            auto span = m::make_span(buffer, buffer_size);
            utf16_to_multi_byte(cp, view, span);
            return span.size();
        });
        using std::swap;
        swap(t, str);
    }

    void
    utf16_to_acp(std::wstring_view view, std::string& string)
    {
        utf16_to_multi_byte(multi_byte::cp_acp, view, string);
    }

    void
    utf16_to_acp(std::u16string_view view, std::string& string)
    {
        utf16_to_multi_byte(multi_byte::cp_acp, view, string);
    }

} // namespace m

void
m::to_string(m::multi_byte::code_page cp, std::wstring_view view, std::string& str)
{
    m::utf16_to_multi_byte(cp, view, str);
}

std::string
m::to_string(m::multi_byte::code_page cp, std::wstring_view view)
{
    std::string str;
    to_string(cp, view, str);
    return str;
}

void
m::to_string(m::multi_byte::code_page /* cp*/, std::u8string_view /* view*/, std::string& str)
{
    str.erase();
    throw std::runtime_error("not yet implemented");
    // m::multi_byte::utf8_to_multi_byte(cp, view, str);
}

std::string
m::to_string(m::multi_byte::code_page cp, std::u8string_view view)
{
    std::string str;
    to_string(cp, view, str);
    return str;
}

void
m::to_string(m::multi_byte::code_page cp, std::u16string_view view, std::string& str)
{
    m::utf16_to_multi_byte(cp, view, str);
}

std::string
m::to_string(m::multi_byte::code_page cp, std::u16string_view view)
{
    std::string str;
    to_string(cp, view, str);
    return str;
}

void
m::to_string(m::multi_byte::code_page, std::u32string_view, std::string& str)
{
    str.erase();
    M_NOT_IMPLEMENTED("UTF-32 to CP_ACP conversion not implemented");
    // m::multi_byte::utf32_to_multi_byte(cp, view, str);
}

std::string
m::to_string(m::multi_byte::code_page cp, std::u32string_view view)
{
    std::string str;
    to_string(cp, view, str);
    return str;
}

namespace m::multi_byte::details
{
    std::size_t
    utf16_to_multi_byte(code_page cp, std::u16string_view in, std::span<char>& out)
    {
        return impl::utf16_to_multi_byte_fn(
            cp, std::wstring_view(reinterpret_cast<wchar_t const*>(in.data()), in.size()), out);
    }

    std::size_t
    utf16_to_multi_byte(code_page cp, std::wstring_view in, std::span<char>& out)
    {
        return impl::utf16_to_multi_byte_fn(cp, in, out);
    }

} // namespace m::multi_byte::details