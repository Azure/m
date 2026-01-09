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

namespace m::multi_byte::impl
{
    template <typename TCharIn>
    requires m::utf16_character<TCharIn>
    void
    utf16_to_multi_byte_fn(code_page                          cp,
                           std::basic_string_view<TCharIn> view,
                           std::span<char>&                   buffer,
                           std::error_code&                   ec)
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

        auto i = ::WideCharToMultiByte(to_underlying(cp),
                                       WC_NO_BEST_FIT_CHARS,
                                       reinterpret_cast<wchar_t const*>(view.data()),
                                       to<int>(view.size()),
                                       buffer.data(),
                                       to<int>(buffer.size()),
                                       nullptr,  // lpDefaultChar
                                       nullptr); // lpUsedDefaultChar
        if (i < 1)
        {
            ec = get_last_win32_error();
            return;
        }

        buffer = buffer.subspan(0, i);
    }

    template <typename TCharIn>
    void
    utf16_to_multi_byte_fn(code_page                          cp,
                           std::basic_string_view<TCharIn> view,
                           std::span<char>&                   buffer)
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

        auto i = ::WideCharToMultiByte(to_underlying(cp),
                                       WC_NO_BEST_FIT_CHARS,
                                       reinterpret_cast<wchar_t const*>(view.data()),
                                       to<int>(view.size()),
                                       buffer.data(),
                                       to<int>(buffer.size()),
                                       nullptr,  // lpDefaultChar
                                       nullptr); // lpUsedDefaultChar
        if (i < 1)
            m::throw_last_win32_error();

        buffer = buffer.subspan(0, i);
    }

    template <typename TCharIn>
        requires utf16_character<TCharIn>
    [[nodiscard]]
    std::string
    utf16_view_to_multi_byte_fn(code_page cp, std::basic_string_view<TCharIn> view)
    {
        std::string result;

        auto length = m::utf16_to_multi_byte_length(cp, view);
        result.resize_and_overwrite(length, [&](auto buffer, auto size) -> auto {
            auto span = make_span(buffer, size);
            multi_byte::impl::utf16_to_multi_byte_fn(cp, view, span);
            return span.size();
        });
        return result;
    }
} // namespace m::multi_byte::impl

namespace m
{
    template <>
    void
    utf16_to_multi_byte(multi_byte::code_page cp, std::wstring_view view, std::span<char>& buffer)
    {
        multi_byte::impl::utf16_to_multi_byte_fn(cp, view, buffer);
    }

    template <>
    void
    utf16_to_multi_byte(multi_byte::code_page cp, std::u16string_view view, std::span<char>& buffer)
    {
        multi_byte::impl::utf16_to_multi_byte_fn(cp, view, buffer);
    }

    template <>
    void
    utf16_to_multi_byte(multi_byte::code_page cp,
                        std::wstring_view     view,
                        std::span<char>&      buffer,
                        std::error_code&      ec)
    {
        multi_byte::impl::utf16_to_multi_byte_fn(cp, view, buffer, ec);
    }

    template <>
    void
    utf16_to_multi_byte(multi_byte::code_page cp,
                        std::u16string_view   view,
                        std::span<char>&      buffer,
                        std::error_code&      ec)
    {
        multi_byte::impl::utf16_to_multi_byte_fn(cp, view, buffer, ec);
    }

    template <>
    void
    utf16_to_multi_byte(multi_byte::code_page cp, std::wstring_view in, std::string& out)
    {
        auto const length = utf16_to_multi_byte_length(cp, in);
        out.resize_and_overwrite(length, [cp, in](auto buffer, auto buffer_size) -> auto {
            auto span = make_span(buffer, buffer_size);
            utf16_to_multi_byte(cp, in, span);
            return span.size();
        });
    }

    template <>
    void
    utf16_to_multi_byte(multi_byte::code_page cp, std::u16string_view in, std::string& out)
    {
        auto const length = utf16_to_multi_byte_length(cp, in);
        out.resize_and_overwrite(length, [cp, in](auto buffer, auto buffer_size) -> auto {
            auto span = make_span(buffer, buffer_size);
            utf16_to_multi_byte(cp, in, span);
            return span.size();
        });
    }

    template <>
    void
    utf16_to_multi_byte(multi_byte::code_page cp,
                        std::u16string_view   in,
                        std::string&          out,
                        std::error_code&      ec)
    {
        auto const length = utf16_to_multi_byte_length(cp, in, ec);
        if (failed(ec))
            return;

        out.resize_and_overwrite(length, [cp, in, &ec](auto buffer, auto buffer_size) -> auto {
            auto span = make_span(buffer, buffer_size);
            utf16_to_multi_byte(cp, in, span, ec);
            return span.size();
        });
    }

    template <>
    void
    utf16_to_multi_byte(multi_byte::code_page cp,
                        std::wstring_view     in,
                        std::string&          out,
                        std::error_code&      ec)
    {
        auto const length = utf16_to_multi_byte_length(cp, in, ec);
        if (failed(ec))
            return;

        out.resize_and_overwrite(length, [cp, in, &ec](auto buffer, auto buffer_size) -> auto {
            auto span = make_span(buffer, buffer_size);
            utf16_to_multi_byte(cp, in, span, ec);
            return span.size();
        });
    }

#if 0
    std::size_t
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
        return str.size();
    }

    template <>
    void
    utf16_to_multi_byte(multi_byte::code_page cp,
                        std::wstring_view     in,
                        std::span<char>&      out,
                        std::error_code&      ec)
    {
        multi_byte::impl::utf16_to_multi_byte_fn(cp, in, out, ec);
    }
#endif



} // namespace m
